// SPDX-License-Identifier: Apache-2.0
#include "passive_role_handler.hpp"

#include "persistent_data.hpp"
#include "util.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Control/Failover/common.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

namespace rbmc
{

constexpr auto bmcPassiveTarget = "obmc-bmc-passive.target";

using Failover = sdbusplus::common::xyz::openbmc_project::control::Failover;
using Redundancy =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

PassiveRoleHandler::PassiveRoleHandler(sdbusplus::async::context& ctx,
                                       Providers& providers,
                                       RedundancyInterface& iface) :
    RoleHandler(ctx, providers, iface)
{
    try
    {
        // If passiveDueToError is true, then this BMC can never
        // be active.  Add a value into ReasonsForNoRedundancy so
        // the active can get it and not enable redundancy.
        if (data::read<bool>(data::key::passiveError).value_or(false))
        {
            lg2::info(
                "Setting 'SiblingCannotBeActive' in ReasonsForNoRedundancy property");
            iface.reasons_for_no_redundancy(
                {Redundancy::ReasonForNoRedundancy::SiblingCannotBeActive});
        }
        else
        {
            iface.reasons_for_no_redundancy({});
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain PassiveDueToError: {ERROR}",
                   "ERROR", e);
        iface.reasons_for_no_redundancy({});
    }

    util::clearExternalRedundancyInputs();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::start()
{
    try
    {
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        co_await providers.getServices().startUnit(bmcPassiveTarget,
                                                   std::chrono::minutes{5});
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed while starting BMC passive target: {ERROR}", "ERROR",
                   e);
    }

    // Setup the mirroring of the active BMC RedundancyEnabled
    setupSiblingRedEnabledWatch();

    setupSiblingFailoversAllowedWatch();

    setupSiblingHealthWatch();

    setupPeerConnectedWatch();

    try
    {
        // This is only valid on the active BMC
        data::remove(data::key::redundancyOffAtRuntime);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed while removing RedundancyOffAtRuntime saved value: {ERROR}",
            "ERROR", e);
    }

    try
    {
        // This is only valid on the active BMC
        data::remove(data::key::codeUpdateInProgress);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed while removing CodeUpdateInProgress saved value: {ERROR}",
            "ERROR", e);
    }

    co_return;
}

void PassiveRoleHandler::setupSiblingRedEnabledWatch()
{
    auto& sibling = providers.getSibling();

    // Register for changes
    sibling.addRedundancyEnabledCallback(Role::Passive, [this](bool enabled) {
        siblingRedEnabledHandler(enabled);
    });

    // Handle current value
    auto sibRedEnabled = sibling.getRedundancyEnabled();
    if (sibRedEnabled.has_value())
    {
        siblingRedEnabledHandler(sibRedEnabled.value());
    }
    else
    {
        // No sibling right now, make sure sync is off
        ctx.spawn(stopSync());
    }
}

void PassiveRoleHandler::setupSiblingFailoversAllowedWatch()
{
    auto& sibling = providers.getSibling();

    // Register for changes
    sibling.addFailoversAllowedCallback(Role::Passive, [this](bool allowed) {
        siblingFailoversAllowedHandler(allowed);
    });

    // Handle current value
    auto sibAllowed = sibling.getFailoversAllowed();
    if (sibAllowed.has_value())
    {
        siblingFailoversAllowedHandler(sibAllowed.value());
    }
}

void PassiveRoleHandler::siblingRedEnabledHandler(bool enable)
{
    // If the sibling is Active, mirror the property on this BMC.
    if (providers.getSibling().getRole().value_or(Role::Unknown) ==
        Role::Active)
    {
        redundancyInterface.redundancy_enabled(enable);
    }

    // Kick off a full sync if possible
    ctx.spawn(tryFullSync());
}

void PassiveRoleHandler::siblingFailoversAllowedHandler(bool allowed)
{
    // If the sibling is Active, mirror the property on this BMC.
    // TODO: The passive BMC ill have its own reasons for not allowing
    // failovers that also need to be considered.
    if (providers.getSibling().getRole().value_or(Role::Unknown) ==
        Role::Active)
    {
        redundancyInterface.failovers_allowed(allowed);
    }
}

void PassiveRoleHandler::disableRedPropChanged(bool /*disable*/)
{
    lg2::error("Cannot modify DisableRedundancy property on passive BMC");
    throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
}

void PassiveRoleHandler::externalRedundancyInputChanged()
{
    // Not supported on the passive BMC
    throw sdbusplus::xyz::openbmc_project::Common::Error::NotAllowed();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::tryFullSync()
{
    if (providers.getSibling().alive() &&
        providers.getSibling().getRedundancyEnabled().value_or(false) &&
        (providers.getSibling().getRole().value_or(Role::Unknown) ==
         Role::Active) &&
        providers.getServices().getPeerConnected())
    {
        co_await startSync();
    }
    else
    {
        co_await stopSync();
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::startSync()
{
    if (fullSyncDone)
    {
        co_return;
    }

    bool syncSucceeded = false;

    try
    {
        syncSucceeded = co_await providers.getSyncInterface().doFullSync();
        if (!syncSucceeded)
        {
            lg2::error("Full sync on passive BMC failed");
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Full sync on passive BMC failed with exception: {ERROR}",
                   "ERROR", e);
        syncSucceeded = false;
    }

    if (syncSucceeded)
    {
        fullSyncDone = true;

        providers.getSyncInterface().watchSyncHealth(
            Role::Passive,
            [this](auto health) { syncHealthPropertyChanged(health); });
    }
    else
    {
        co_await stopSync();
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::stopSync()
{
    fullSyncDone = false;
    providers.getSyncInterface().stopSyncHealthWatch(Role::Passive);
    co_await providers.getSyncInterface().disableBackgroundSync();
}

void PassiveRoleHandler::syncHealthPropertyChanged(
    SyncBMCData::SyncEventsHealth health)
{
    lg2::info("Passive BMC sync health property changed to {HEALTH}", "HEALTH",
              health);

    if (health == SyncBMCData::SyncEventsHealth::Critical)
    {
        // Don't care about fails if no redundancy.
        if (!redundancyInterface.redundancy_enabled())
        {
            lg2::info("Redundancy isn't enabled so don't care about sync fail");
            return;
        }

        ctx.spawn(syncHealthCritical());
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::syncHealthCritical()
{
    lg2::warning("Disabling sync because it is failing");
    co_await stopSync();

    // Redundancy doesn't need to be disabled if a background sync fails on a
    // passive BMC. Still wait to see if it was caused by loss of the active
    // BMC via a heartbeat check, so we know what happened.
    // TODO: Also do network failure detection.

    lg2::info("Waiting to see if sibling heartbeat stops");
    co_await providers.getSibling().pauseForHeartbeatChange();

    if (providers.getSibling().alive())
    {
        lg2::error("Sync fail was not caused by a sibling BMC problem");
    }
    else
    {
        lg2::warning(
            "Sync health is critical, but there is also a sibling heartbeat loss");
    }
}

void PassiveRoleHandler::siblingHealthChange(bool alive)
{
    lg2::info("Sibling health changed to {ALIVE}", "ALIVE", alive);

    if (alive)
    {
        // Probably redundancy would be disabled here,
        // but try anyway just in case.
        ctx.spawn(tryFullSync());
    }
    else
    {
        ctx.spawn(stopSync());
    }
}

// NOLINTNEXTLINE
auto PassiveRoleHandler::getFailoverBlockedReason(
    const FailoverOptions& options)
    -> sdbusplus::async::task<fo_blocked::Reason>
{
    auto force =
        util::getFailoverOption<bool>(Failover::Options::Force, options)
            .value_or(false);

    BMCState bmcState{BMCState::NotReady};

    try
    {
        bmcState = co_await providers.getServices().getBMCState();
    }
    catch (const std::exception& e)
    {
        lg2::error("The call to get the BMC state failed: {ERROR}", "ERROR", e);
    }

    auto& sibling = providers.getSibling();

    fo_blocked::PassiveInput input{
        .siblingAlive = sibling.alive(),
        .siblingState = sibling.getBMCState().value_or(BMCState::NotReady),
        .redundancyEnabled = sibling.getRedundancyEnabled().value_or(false),
        .state = bmcState,
        .failoversNotAllowed = !redundancyInterface.failovers_allowed(),
        .forceOption = force,
        .failoverInProgress = redundancyInterface.failover_in_progress() ||
                              redundancyInterface.failover_imminent(),

        // If the active BMC were to completely die, its last known value of
        // RedundancyEnabled will still have been mirrored to the passive.
        // This will be used to know if a failover is still OK without live
        // data from the active BMC.
        .lastKnownRedundancyEnabled = redundancyInterface.redundancy_enabled()};

    co_return fo_blocked::getPassiveFailoverBlockedReason(input);
}

void PassiveRoleHandler::peerConnectedChange([[maybe_unused]] bool connected)
{
    // If connected and all other conditions succeed for a full sync,
    // will start one, otherwise will stop background syncing.
    // If the active BMC just came back from a reboot, most likely
    // redundancy won't be enabled yet though.
    ctx.spawn(tryFullSync());
}

} // namespace rbmc
