// SPDX-License-Identifier: Apache-2.0

#include "redundancy_mgr.hpp"

#include <persistent_data.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

RedundancyMgr::RedundancyMgr(sdbusplus::async::context& ctx, Services& services,
                             Sibling& sibling, RedundancyInterface& iface) :
    ctx(ctx), services(services), sibling(sibling), redundancyInterface(iface),
    manualDisable(iface.disable_redundancy_override())
{
    try
    {
        data::remove(data::key::noRedDetails);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed removing NoRedundancyDetails: {ERROR}", "ERROR", e);
    }
}

// NOLINTNEXTLINE
void RedundancyMgr::determineAndSetRedundancy()
{
    if (!redundancyDetermined)
    {
        initSystemState();
    }

    enableOrDisableRedundancy(getNoRedundancyReasons());
    redundancyDetermined = true;
}

redundancy::NoRedundancyReasons RedundancyMgr::getNoRedundancyReasons()
{
    redundancy::Input input{
        .role = redundancyInterface.role(),
        .siblingPresent = sibling.isBMCPresent(),
        .siblingHeartbeat = sibling.hasHeartbeat(),
        .siblingProvisioned = sibling.getProvisioned().value_or(false),
        .siblingHasSiblingComm = sibling.getSiblingCommsOK().value_or(false),
        .siblingRole = sibling.getRole().value_or(Role::Unknown),
        .siblingState = sibling.getBMCState().value_or(BMCState::NotReady),
        .codeVersionsMatch =
            services.getFWVersion() == sibling.getFWVersion().value_or(""),
        .manualDisable = manualDisable,
        .redundancyOffAtRuntimeStart = isRedundancyOffAtRuntime()};

    auto reasons = redundancy::getNoRedundancyReasons(input);

    std::map<redundancy::NoRedundancyReason, std::string> details;

    std::ranges::transform(
        reasons, std::inserter(details, details.begin()),
        [](const auto& reason) {
            auto desc = redundancy::getNoRedundancyDescription(reason);
            lg2::info("No redundancy because: {DESC}", "DESC", desc);
            return std::pair{reason, desc};
        });

    try
    {
        data::write(data::key::noRedDetails, details);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed serializing NoRedundancyReasons: {ERROR}", "ERROR",
                   e);
    }

    return reasons;
}

void RedundancyMgr::enableOrDisableRedundancy(
    const redundancy::NoRedundancyReasons& disableReasons)
{
    auto enable = disableReasons.empty();

    if (enable)
    {
        lg2::info("Enabling redundancy");
    }
    else
    {
        lg2::info("Redundancy must be disabled");
    }

    redundancyInterface.redundancy_enabled(enable);
}

void RedundancyMgr::disableRedPropChanged(bool disable)
{
    if (systemState.value_or(SystemState::other) != SystemState::off)
    {
        lg2::error("Cannot modify DisableRedundancy prop when powered on");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    manualDisable = disable;

    if (!redundancyDetermined)
    {
        // Must be before we've handled redundancy, it should happen soon
        lg2::info(
            "Redundancy has not been determined yet, will not change redundancy now.");
        return;
    }

    if (disable == !redundancyInterface.redundancy_enabled())
    {
        // No changes necessary now
        lg2::info("No change to redundancy necessary");
        return;
    }

    lg2::info(
        "Revisiting redundancy after manual override of disable to {DISABLE}",
        "DISABLE", disable);

    determineAndSetRedundancy();
}

void RedundancyMgr::initSystemState()
{
    services.addSystemStateCallback(
        std::bind_front(&RedundancyMgr::systemStateChange, this));

    try
    {
        systemState = services.getSystemState();

        lg2::info("RedundancyMgr: Initial system state is {STATE}", "STATE",
                  Services::getSystemStateName(
                      systemState.value_or(SystemState::other)));
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
              Services::getSystemStateName(newState));

    if (newState == SystemState::off)
    {
        clearRedundancyOffAtRuntime();
    }
    else if (newState == SystemState::booting)
    {
        // TODO: failovers paused, etc
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

    // TODO: on any change recalculate failovers paused
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

} // namespace rbmc
