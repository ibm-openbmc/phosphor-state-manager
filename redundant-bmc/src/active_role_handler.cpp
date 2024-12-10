// SPDX-License-Identifier: Apache-2.0
#include "active_role_handler.hpp"

#include "redundancy.hpp"

#include <persistent_data.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

constexpr auto bmcActiveTarget = "obmc-bmc-active.target";

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::start()
{
    try
    {
        data::remove(data::key::noRedDetails);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed removing NoRedundancyDetails: {ERROR}", "ERROR", e);
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

    if (sibling.hasHeartbeat())
    {
        // Make sure the sibling had time to get its role assigned, and
        // also wait for it to hit steady state as redundancy can only
        // be enabled if the sibling BMC is at the Ready state.
        co_await sdbusplus::async::execution::when_all(
            sibling.waitForSiblingRole(), sibling.waitForBMCSteadyState());
    }

    determineAndSetRedundancy();

    // TODO: error log if redundancy is disabled

    co_return;
}

// NOLINTNEXTLINE
void ActiveRoleHandler::determineAndSetRedundancy()
{
    enableOrDisableRedundancy(getNoRedundancyReasons());
    redundancyDetermined = true;
}

redundancy::NoRedundancyReasons ActiveRoleHandler::getNoRedundancyReasons()
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
        .manualDisable = manualDisable};

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

void ActiveRoleHandler::enableOrDisableRedundancy(
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

void ActiveRoleHandler::disableRedPropChanged(bool disable)
{
    try
    {
        if (services.isPoweredOn())
        {
            lg2::error("Cannot modify DisableRedundancy prop when powered on");
            throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Call to isPoweredOn failed: {ERROR}", "ERROR", e);
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

} // namespace rbmc
