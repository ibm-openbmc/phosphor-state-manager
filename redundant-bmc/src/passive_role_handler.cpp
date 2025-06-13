// SPDX-License-Identifier: Apache-2.0
#include "passive_role_handler.hpp"

#include "persistent_data.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

constexpr auto bmcPassiveTarget = "obmc-bmc-passive.target";

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::start()
{
    try
    {
        // NOLINTNEXTLINE
        co_await providers.getServices().startUnit(bmcPassiveTarget);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // TODO: error log
        lg2::error("Failed while starting BMC passive target: {ERROR}", "ERROR",
                   e);
    }

    // Setup the mirroring of the active BMC RedundancyEnabled
    setupSiblingRedEnabledWatch();

    setupSiblingFailoversAllowedWatch();

    setupSiblingHBWatch();

    try
    {
        // Only the active needs NoRedundancyDetails persisted.
        data::remove(data::key::noRedDetails);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed while removing NoRedundancyDetails saved value: {ERROR}",
            "ERROR", e);
    }

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

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::tryFullSync()
{
    if (providers.getSibling().hasHeartbeat() &&
        providers.getSibling().getRedundancyEnabled().value_or(false) &&
        (providers.getSibling().getRole().value_or(Role::Unknown) ==
         Role::Active))
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

    if (co_await providers.getSyncInterface().doFullSync())
    {
        fullSyncDone = true;

        providers.getSyncInterface().watchSyncHealth(
            Role::Passive,
            [this](auto health) { syncHealthPropertyChanged(health); });
    }
    else
    {
        lg2::error("Full sync on passive BMC failed");
        co_await stopSync();
        // TODO: create error log
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
    lg2::info("Disabling sync because it is failing");
    co_await stopSync();

    // Redundancy doesn't need to be disabled if a background sync fails on a
    // passive BMC. Still wait to see if it was caused by loss of the active
    // BMC via a heartbeat check, so we know what happened.
    // TODO: Also do network failure detection.

    lg2::info("Waiting to see if sibling heartbeat stops");
    co_await providers.getSibling().pauseForHeartbeatChange();

    if (providers.getSibling().hasHeartbeat())
    {
        lg2::error("Sync fail was not caused by a sibling BMC problem");
        // TODO: Create error log
    }
    else
    {
        lg2::info(
            "Sync health is critical, but there is also a sibling heartbeat loss");
    }
}

void PassiveRoleHandler::siblingHBChange(bool hb)
{
    lg2::info("Sibling heartbeat changed to {HB}", "HB", hb);

    if (hb)
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

} // namespace rbmc
