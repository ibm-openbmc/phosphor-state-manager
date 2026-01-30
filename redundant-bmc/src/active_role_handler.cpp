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
        // NOLINTNEXTLINE
        co_await services.startUnit(bmcActiveTarget);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // TODO: error log
        lg2::error("Failed while starting BMC active target: {ERROR}", "ERROR",
                   e);
    }

    if (sibling.alive())
    {
        // Make sure the sibling had time to get its role assigned, and
        // also wait for it to hit steady state as redundancy can only
        // be enabled if the sibling BMC is at the Ready state.
        co_await sdbusplus::async::execution::when_all(
            sibling.waitForSiblingRole(), sibling.waitForBMCSteadyState());
    }

    co_await redMgr.determineRedundancyAndSync();

    // TODO: Create an error if no redundancy

    startSiblingWatches();

    startSyncHealthWatch();

    co_return;
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
        siblingHealthTimer.stop();
        ctx.spawn(siblingHealthy());
    }
    else
    {
        lg2::info("Sibling BMC health changed to bad");
        if (redundancyInterface.redundancy_enabled())
        {
            lg2::info(
                "Disabling redundancy in {TIME} minutes if sibling doesn't come back",
                "TIME", siblingHealthTimeout.count());
            siblingHealthTimer.start(siblingHealthTimeout);
        }
    }
}

void ActiveRoleHandler::siblingHealthCritical()
{
    lg2::error("Sibling health timer expired, disabling redundancy");
    redMgr.determineAndSetRedundancy();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::siblingHealthy()
{
    lg2::info("Passive BMC health changed to good");

    stopSiblingWatches();

    auto& sibling = providers.getSibling();
    co_await sdbusplus::async::execution::when_all(
        sibling.waitForSiblingRole(), sibling.waitForBMCSteadyState());

    lg2::info("Attempting to enable redundancy now that sibling is back");
    providers.getSyncInterface().clearFullSyncComplete();
    co_await redMgr.determineRedundancyAndSync();

    startSiblingWatches();

    co_return;
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

    lg2::info("Disabling background sync because it is failing");
    co_await providers.getSyncInterface().disableBackgroundSync();

    // A passive BMC reboot should not result in redundancy being
    // disabled, so wait a bit for the passive BMC's heartbeat
    // to change.  If it's still running, then this is a valid
    // sync fail so disable redundancy.  If it isn't running then
    // then the code that deals with the sibling heartbeat will
    // deal with it.
    // TODO: Also do network failure detection.
    lg2::info("Waiting to see if sibling heartbeat stops");
    co_await providers.getSibling().pauseForHeartbeatChange();

    if (providers.getSibling().alive())
    {
        lg2::error("Disabling redundancy due to critical sync health");

        // This will disable redundancy
        redMgr.handleBackgroundSyncFailed();
    }
    else
    {
        lg2::info(
            "Sync health is critical, but there is also a sibling heartbeat loss");
    }

    co_return;
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
        co_await providers.getServices().startUnit(bmcActiveTarget);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // TODO: error log
        lg2::error(
            "Failed while starting BMC active target during failover: {ERROR}",
            "ERROR", e);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::failoverWaitForSibling()
{
    // Wait a bit to ensure the sibling reset is detected before we
    // start waiting for it to come back. After the active target
    // has real code in it that runs longer than a few seconds,
    // this could be removed.
    co_await providers.getSibling().pauseForHeartbeatChange();

    // Wait for the sibling heartbeat to come back
    co_await providers.getSibling().waitForSiblingUp();

    // Wait for the sibling to get to a steady state so redundancy can
    // be determined.
    co_await providers.getSibling().waitForBMCSteadyState();
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

    startSiblingWatches();
    startSyncHealthWatch();
}

void ActiveRoleHandler::siblingFailoverImminent(bool imminent)
{
    if (imminent)
    {
        lg2::info("A failover is imminent");

        ctx.spawn(providers.getSyncInterface().disableBackgroundSync());
        ctx.spawn(providers.getServices().flushJournal());
    }
}

} // namespace rbmc
