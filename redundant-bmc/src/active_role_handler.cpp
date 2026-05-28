// SPDX-License-Identifier: Apache-2.0
#include "active_role_handler.hpp"

#include "persistent_data.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

constexpr auto bmcActiveTarget = "obmc-bmc-active.target";
const std::chrono::minutes siblingHealthTimeout{5};

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::start()
{
    auto& services = providers.getServices();
    auto& sibling = providers.getSibling();

    try
    {
        data::write(data::key::passiveError, false);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed clearing the PassiveDueToError field: {ERROR}",
                   "ERROR", e);
    }

    try
    {
        lg2::info("Acquiring hardware access");
        co_await services.acquireFullHardwareAccess();
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed acquiring hardware access: {ERROR}", "ERROR", e);
    }

    try
    {
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        co_await services.startUnit(bmcActiveTarget, std::chrono::minutes{10});
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed while starting BMC active target: {ERROR}", "ERROR",
                   e);
    }

    if (sibling.alive())
    {
        // Before trying to enable redundancy, wait for:
        // 1. Sibling to have its role assigned.
        // 2. Sibling to hit steady state (Ready needed for redundancy)
        // 3. The network to connect to the sibling BMC.
        co_await sdbusplus::async::execution::when_all(
            sibling.waitForSiblingRole(), sibling.waitForBMCSteadyState(),
            services.waitForPeerConnection(std::bind_front(
                &ActiveRoleHandler::canStopPeerConnectionWait, this)));

        // If PeerConnected == true and sibling paired == false
        // a small delay will be needed to let the new paired
        // value propagate to this BMC.
        if (services.getPeerConnected() && !sibling.getPaired().value_or(true))
        {
            co_await sibling.pauseForDataPropagation();
        }
    }

    co_await redMgr.determineRedundancyAndSync();

    startAllWatches();
}

void ActiveRoleHandler::siblingStateChange(BMCState state)
{
    if (state == BMCState::Quiesced)
    {
        lg2::error("Sibling BMC went to Quiesce, disabling redundancy");
        redMgr.determineAndSetRedundancy();
    }
}

void ActiveRoleHandler::siblingHealthChange(bool alive)
{
    if (alive)
    {
        lg2::info("Sibling BMC health changed to good");
        siblingHealthTimer.stop();
        ctx.spawn(siblingHealthy());
    }
    else
    {
        lg2::warning("Sibling BMC health changed to bad");
        if (redundancyInterface.redundancy_enabled())
        {
            lg2::warning(
                "Disabling redundancy in {TIME} minutes if sibling doesn't come back",
                "TIME", siblingHealthTimeout.count());

            // Just use the sibling health timer if neither indicator is good.
            if (peerConnectionTimer.isRunning())
            {
                lg2::info("Stopping peer connection timer");
                peerConnectionTimer.stop();
            }

            siblingHealthTimer.start(siblingHealthTimeout);

            // Background sync won't work without a healthy sibling
            ctx.spawn(providers.getSyncInterface().disableBackgroundSync());
        }
    }
}

void ActiveRoleHandler::siblingHealthCritical()
{
    lg2::error("Sibling health timer expired, disabling redundancy");
    redMgr.determineAndSetRedundancy();
}

sdbusplus::async::task<> ActiveRoleHandler::siblingHealthy()
{
    stopAllWatches();

    auto& sibling = providers.getSibling();
    auto& services = providers.getServices();

    // As redundancy could be enabled here if passive
    // wasn't gone long, hold off failovers until the
    // full sync is done.
    providers.getSyncInterface().clearFullSyncComplete();
    redMgr.determineAndSetFailoversAllowed();

    // Before trying to enable redundancy, wait for:
    // 1. Sibling to have its role assigned.
    // 2. Sibling to hit steady state (Ready needed for redundancy)
    // 3. Peer connection to be established.
    co_await sdbusplus::async::execution::when_all(
        sibling.waitForSiblingRole(), sibling.waitForBMCSteadyState(),
        services.waitForPeerConnection(std::bind_front(
            &ActiveRoleHandler::canStopPeerConnectionWait, this)));

    // Just like in start(), a delay may be needed to let
    // the sibling paired value propagate.
    if (services.getPeerConnected() && !sibling.getPaired().value_or(true))
    {
        co_await sibling.pauseForDataPropagation();
    }

    lg2::info("Attempting to enable redundancy now that sibling is back");
    co_await redMgr.determineRedundancyAndSync();

    startAllWatches();
}

void ActiveRoleHandler::peerConnectionChange(bool connected)
{
    lg2::info("PeerConnected changed to {C}", "C", connected);

    if (connected)
    {
        peerConnectionTimer.stop();

        // If sibling is already alive, attempt to re-enable redundancy.
        if (providers.getSibling().alive())
        {
            ctx.spawn(siblingHealthy());
        }
    }
    else
    {
        if (!redundancyInterface.redundancy_enabled())
        {
            return;
        }

        // Always disable background sync when network is down
        ctx.spawn(providers.getSyncInterface().disableBackgroundSync());

        // If sibling is alive, this is only a network issue at this
        // point so start the timer to disabling redundancy. Otherwise,
        // this problem is already being handled by the sibling
        // health timer code.
        if (providers.getSibling().alive())
        {
            lg2::warning(
                "Disabling redundancy in {TIME} minutes if peer connection doesn't come back",
                "TIME", siblingHealthTimeout.count());
            peerConnectionTimer.start(siblingHealthTimeout);
        }
    }
}

void ActiveRoleHandler::peerConnectionCritical()
{
    lg2::error("Peer connection timer expired, disabling redundancy");
    // Disables redundancy because peerConnected is false
    redMgr.determineAndSetRedundancy();
}

void ActiveRoleHandler::syncHealthPropertyChanged(
    SyncBMCData::SyncEventsHealth health)
{
    lg2::info("Sync health property changed to {HEALTH}", "HEALTH", health);

    // Don't care about changes if no redundancy.
    if (!redundancyInterface.redundancy_enabled())
    {
        return;
    }

    if (health == SyncBMCData::SyncEventsHealth::Critical)
    {
        ctx.spawn(syncHealthCritical());
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::syncHealthCritical()
{
    using namespace std::chrono_literals;

    lg2::warning("Disabling background sync because it is failing");
    co_await providers.getSyncInterface().disableBackgroundSync();

    // A passive BMC reboot should not result in redundancy being
    // disabled, so wait a bit for the passive BMC's heartbeat
    // to change to see if that is the cause.
    // After that:
    // - If heartbeat (alive) and peer connection are OK, then
    //   disable redundancy due to a sync fail.
    //   If heartbeat or peer connection are bad then then the code
    //   that deals with those will handle everything.

    lg2::info("Waiting to see if sibling heartbeat stops");
    co_await providers.getSibling().pauseForHeartbeatChange();

    if (providers.getSibling().alive() &&
        providers.getServices().getPeerConnected())
    {
        lg2::error("Disabling redundancy due to critical sync health");

        // This will disable redundancy
        redMgr.handleBackgroundSyncFailed();
    }
    else
    {
        // The siblingHealthTimer/peerConnectionTimer will handle this.
        lg2::warning(
            "Sync fail caused by sibling alive = {ALIVE} or peer connected = {CONN}",
            "ALIVE", providers.getSibling().alive(), "CONN",
            providers.getServices().getPeerConnected());
    }
}

// NOLINTNEXTLINE
auto ActiveRoleHandler::getFailoverBlockedReason(
    [[maybe_unused]] const FailoverOptions& options)
    -> sdbusplus::async::task<fo_blocked::Reason>
{
    // At some point in the future we may allow triggering
    // a failover from the active BMC, but not at the moment.
    lg2::error("Active BMC cannot trigger a failover now");
    co_return fo_blocked::Reason::bmcNotPassive;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::failoverStartActiveTarget()
{
    try
    {
        lg2::info("Acquiring hardware access");
        co_await providers.getServices().acquireFullHardwareAccess();
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed acquiring hardware access during failover: {ERROR}",
                   "ERROR", e);
    }

    try
    {
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        co_await providers.getServices().startUnit(bmcActiveTarget,
                                                   std::chrono::minutes{10});
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed while starting BMC active target during failover: {ERROR}",
            "ERROR", e);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::failoverWaitForSibling()
{
    auto& sibling = providers.getSibling();
    auto& services = providers.getServices();

    // Wait a bit to ensure the sibling reset is detected before we
    // start waiting for it to come back. After the active target
    // has real code in it that runs longer than a few seconds,
    // this could be removed.
    co_await sibling.pauseForHeartbeatChange();

    // Wait for the sibling heartbeat to come back
    co_await sibling.waitForSiblingUp();

    // If heartbeat came back, wait for steady state and the network.
    if (sibling.alive())
    {
        co_await sdbusplus::async::execution::when_all(
            sibling.waitForBMCSteadyState(),
            services.waitForPeerConnection(std::bind_front(
                &ActiveRoleHandler::canStopPeerConnectionWait, this)));
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::failoverDetermineRedundancy()
{
    // Reset full sync complete so failovers allowed will
    // stay off until the full sync is done.
    providers.getSyncInterface().clearFullSyncComplete();

    // Tell redMgr failover is complete so it won't block
    // failovers allowed.
    redMgr.clearFailoverInProgress();

    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    co_await redMgr.determineRedundancyAndSync();

    startAllWatches();
}

void ActiveRoleHandler::siblingFailoverImminent(bool imminent)
{
    if (imminent)
    {
        lg2::warning("A failover is imminent");

        ctx.spawn(providers.getSyncInterface().disableBackgroundSync());
        ctx.spawn(providers.getServices().flushJournal());
    }
}

} // namespace rbmc
