// SPDX-License-Identifier: Apache-2.0

#include "redundancy_mgr.hpp"

#include "error_data.hpp"
#include "errors.hpp"

#include <persistent_data.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

RedundancyMgr::RedundancyMgr(sdbusplus::async::context& ctx,
                             Providers& providers, RedundancyInterface& iface) :
    ctx(ctx), providers(providers), redundancyInterface(iface),
    manualDisable(iface.disable_redundancy_override())
{}

void RedundancyMgr::determineAndSetRedundancy()
{
    auto firstTime = !redundancyDetermined;
    auto oldEnabled = redundancyInterface.redundancy_enabled();
    auto oldReasons = redundancyInterface.reasons_for_no_redundancy();

    if (!redundancyDetermined)
    {
        initSystemState();
    }

    auto reasons = getNoRedundancyReasons();
    enableOrDisableRedundancy(reasons);
    redundancyDetermined = true;

    determineAndSetFailoversAllowed();

    if (!redundancyInterface.redundancy_enabled())
    {
        auto wasManuallyDisabled = std::ranges::contains(
            oldReasons, ReasonForNoRedundancy::ManuallyDisabled);

        // Log an error when redundancy isn't enabled if:
        // 1. Redundancy was previously enabled.
        // 2. This is the first time checking redundancy.
        // 3. The manual override to disable redundancy is now off but
        //    was previously on, meaning there is some other reason.
        if (oldEnabled || firstTime || (!manualDisable && wasManuallyDisabled))
        {
            using namespace errors;
            std::string error{error_msg::noRedundancy};
            Level sev{Level::Error};

            if (manualDisable)
            {
                error = error_msg::redundancyManuallyDisabled;
                sev = Level::Informational;
            }

            AdditionalData data;
            data.emplace("ReasonsCount", std::to_string(reasons.size()));
            if (!reasons.empty())
            {
                auto val = std::to_underlying(*reasons.begin());
                data.emplace("FirstReasonVal", std::to_string(val));
            }
            addDefaultData(redundancyInterface, providers, data);

            ctx.spawn(providers.getServices().logError(error, sev, data));
        }

        // Make sure syncs are disabled if redundancy is disabled.
        ctx.spawn(providers.getSyncInterface().disableBackgroundSync());

        providers.getSyncInterface().clearFullSyncComplete();
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> RedundancyMgr::determineRedundancyAndSync()
{
    syncFailed = false;
    determineAndSetRedundancy();

    if (redundancyInterface.redundancy_enabled())
    {
        try
        {
            if (!co_await providers.getSyncInterface().doFullSync())
            {
                lg2::error("Disabling redundancy because full sync failed");
                syncFailed = true;
            }
        }
        catch (const std::exception& e)
        {
            lg2::error("D-Bus error during full sync: {ERROR}", "ERROR", e);
            syncFailed = true;
        }

        if (syncFailed)
        {
            // This will disable redundancy as syncFailed = true
            determineAndSetRedundancy();
            syncFailed = false;
        }
        else
        {
            // Full sync is done so recalculate FailoversAllowed
            determineAndSetFailoversAllowed();
        }
    }

    providers.getServices().setRedundancyDetermined();
}

void RedundancyMgr::handleBackgroundSyncFailed()
{
    syncFailed = true;
    determineAndSetRedundancy();
    syncFailed = false;
}

redundancy::ReasonsForNoRedundancy RedundancyMgr::getNoRedundancyReasons()
{
    auto& sibling = providers.getSibling();
    auto& services = providers.getServices();

    redundancy::Input input{
        .role = redundancyInterface.role(),
        .siblingPresent = sibling.isBMCPresent(),
        .siblingAlive = sibling.alive(),
        .siblingPaired = sibling.getPaired().value_or(false),
        .siblingRole = sibling.getRole().value_or(Role::Unknown),
        .siblingCannotBeActive =
            sibling.getHasReasonForNoRedundancy().value_or(false),
        .siblingState = sibling.getBMCState().value_or(BMCState::NotReady),
        .codeVersionsMatch =
            services.getFWVersion() == sibling.getFWVersion().value_or(""),
        .manualDisable = manualDisable,
        .redundancyOffAtRuntimeStart = isRedundancyOffAtRuntime(),
        .syncFailed = syncFailed,
        .peerConnected = services.getPeerConnected()};

    return redundancy::getNoRedundancyReasons(input);
}

void RedundancyMgr::enableOrDisableRedundancy(
    const redundancy::ReasonsForNoRedundancy& disableReasons)
{
    auto enable = disableReasons.empty();

    if (enable)
    {
        lg2::info("Enabling redundancy");
    }
    else
    {
        lg2::warning("Redundancy must be disabled");
    }

    redundancyInterface.reasons_for_no_redundancy(disableReasons);
    redundancyInterface.redundancy_enabled(enable);
}

void RedundancyMgr::disableRedPropChanged(bool disable)
{
    if (!systemState.has_value())
    {
        lg2::error(
            "Cannot modify DisableRedundancy prop before system state is known");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }
    else if (systemState.value() != SystemState::off)
    {
        lg2::error("Cannot modify DisableRedundancy prop when powered on");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    if (providers.getSyncInterface().isFullSyncInProgress())
    {
        lg2::error(
            "Cannot modify DisableRedundancy when full sync is in progress");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    manualDisable = disable;

    if (!redundancyDetermined)
    {
        // Must be before we've handled redundancy, it should happen soon
        lg2::warning(
            "Redundancy has not been determined yet, will not change redundancy now.");
        return;
    }

    lg2::info(
        "Revisiting redundancy after manual override of disable to {DISABLE}",
        "DISABLE", disable);

    ctx.spawn(determineRedundancyAndSync());
}

void RedundancyMgr::initSystemState()
{
    auto& services = providers.getServices();

    services.addSystemStateCallback(
        Role::Active, std::bind_front(&RedundancyMgr::systemStateChange, this));

    try
    {
        systemState = services.getSystemState();

        lg2::info("RedundancyMgr: Initial system state is {STATE}", "STATE",
                  getSystemStateName(systemState.value_or(SystemState::other)));
    }
    catch (const std::exception& e)
    {
        lg2::error("Could not get system state: {ERROR}", "ERROR", e);
        systemState = SystemState::other;
    }

    // Ensure a value for redundancy off at runtime isn't
    // still valid if system is off, as may have lost AC.
    if (systemState == SystemState::off)
    {
        clearRedundancyOffAtRuntime();
    }
}

void RedundancyMgr::systemStateChange(SystemState newState)
{
    lg2::info("System state change to {NEW}", "NEW",
              getSystemStateName(newState));

    if (newState == SystemState::off)
    {
        clearRedundancyOffAtRuntime();
    }
    else if (newState == SystemState::runtime)
    {
        // Only set if not already valid.  It will need to
        // go through the Off transition to invalidate it
        // before it can be set again.
        if (!isRedundancyOffAtRuntimeValid())
        {
            lg2::info(
                "Locking in runtime redundancy enabled value of {ENABLED}",
                "ENABLED", redundancyInterface.redundancy_enabled());
            setRedundancyOffAtRuntimeValue(
                !redundancyInterface.redundancy_enabled());
        }
    }

    systemState = newState;

    determineAndSetFailoversAllowed();
}

void RedundancyMgr::setRedundancyOffAtRuntime(bool valid, bool off)
{
    std::tuple<bool, bool> value{valid, off};
    try
    {
        data::write(data::key::redundancyOffAtRuntime, value);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed serializing RedundancyOffAtRuntime: {ERROR}",
                   "ERROR", e);
    }
}

std::tuple<bool, bool> RedundancyMgr::getRedundancyOffAtRuntime()
{
    std::tuple<bool, bool> value{false, false};
    try
    {
        value = data::read<decltype(value)>(data::key::redundancyOffAtRuntime)
                    .value_or(std::tuple{false, false});
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain RedundancyOffAtRuntime: {ERROR}",
                   "ERROR", e);
    }
    return value;
}

void RedundancyMgr::determineAndSetFailoversAllowed()
{
    fona::Input input{
        .redundancyEnabled = redundancyInterface.redundancy_enabled(),
        .fullSyncComplete = providers.getSyncInterface().isFullSyncComplete(),
        .failoverInProgress = failoverInProgress,
        .systemState = systemState.value_or(SystemState::other)};

    auto reason = fona::getFailoversNotAllowedReason(input);

    redundancyInterface.failovers_not_allowed_reason(reason);

    if (reason != FailoversNotAllowedReason::None)
    {
        lg2::warning("Failovers not allowed because {REASON}", "REASON",
                     fona::getFailoversNotAllowedDescription(reason));

        redundancyInterface.failovers_allowed(false);
    }
    else
    {
        if (!redundancyInterface.failovers_allowed())
        {
            lg2::info("Changing failovers to allowed");
            redundancyInterface.failovers_allowed(true);
        }
    }
}

void RedundancyMgr::clearFailoversAllowedDuringFailover()
{
    failoverInProgress = true;
    determineAndSetFailoversAllowed();
}

} // namespace rbmc
