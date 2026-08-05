// SPDX-License-Identifier: Apache-2.0
#include "redundancy_interface.hpp"

#include "manager.hpp"
#include "persistent_data.hpp"
#include "phosphor-logging/lg2.hpp"

namespace rbmc
{

const std::string objectPath =
    std::string{RedundancyInterface::namespace_path::value} + '/' +
    RedundancyInterface::namespace_path::bmc;

RedundancyInterface::RedundancyInterface(sdbusplus::async::context& ctx,
                                         Manager& manager,
                                         pcie_data::PCIeStorage* pcieStorage) :
    sdbusplus::aserver::xyz::openbmc_project::state::bmc::Redundancy<
        RedundancyInterface>(ctx, objectPath.c_str()),
    manager(manager), pcieStorage(pcieStorage)
{
    try
    {
        properties.disable_redundancy_override =
            data::read<bool>(data::key::disableRed).value_or(false);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed trying to obtain previous value of DisableRedundancy");
    }

    try
    {
        properties.failover_in_progress =
            data::read<bool>(data::key::failoverInProgress).value_or(false);
        if (properties.failover_in_progress)
        {
            lg2::info("Failover was previously in progress");
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain failover-in-progress: {ERROR}",
                   "ERROR", e);
    }

    // This is recalculated each time.
    try
    {
        data::remove(data::key::noRedReasons);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed removing NoRedundancyReasons: {ERROR}", "ERROR", e);
    }

    // Sync initial D-Bus state to PCIe storage to ensure consistency
    if (pcieStorage != nullptr)
    {
        try
        {
            pcieStorage->writeState(
                {pcie_data::redundancyDataVersion,
                 static_cast<uint8_t>(properties.role),
                 static_cast<uint8_t>(properties.redundancy_enabled),
                 static_cast<uint8_t>(properties.failover_in_progress),
                 static_cast<uint8_t>(properties.failovers_allowed)});
        }
        catch (const std::exception& e)
        {
            lg2::warning("Failed to initialize PCIe storage state: {ERROR}",
                         "ERROR", e);
        }
    }
    else
    {
        lg2::debug(
            "PCIe storage not configured; skipping initial PCIe state sync");
    }

    emit_added();
}

bool RedundancyInterface::set_property([[maybe_unused]] role_t type, Role role)
{
    if (role == properties.role)
    {
        return false;
    }

    properties.role = role;

    if (pcieStorage != nullptr)
    {
        try
        {
            pcieStorage->updateRole(static_cast<uint8_t>(properties.role));
        }
        catch (const std::exception& e)
        {
            lg2::warning("Could not write Role to PCIe memory: {ERROR}",
                         "ERROR", e);
        }
    }

    return true;
}

bool RedundancyInterface::set_property(
    [[maybe_unused]] redundancy_enabled_t type, bool enabled)
{
    if (enabled == redundancy_enabled())
    {
        return false;
    }

    if (pcieStorage != nullptr)
    {
        try
        {
            pcieStorage->updateRedundancyEnabled(enabled);
        }
        catch (const std::exception& e)
        {
            lg2::warning(
                "Could not write RedundancyEnabled to PCIe memory: {ERROR}",
                "ERROR", e);
        }
    }

    properties.redundancy_enabled = enabled;
    return true;
}

bool RedundancyInterface::set_property(
    [[maybe_unused]] disable_redundancy_override_t type, bool disable)
{
    if (disable == disable_redundancy_override())
    {
        return false;
    }

    lg2::info("Request to change DisableRedundancy property to {VALUE}",
              "VALUE", disable);

    manager.disableRedPropChanged(disable);

    try
    {
        data::write(data::key::disableRed, disable);
    }
    catch (const std::exception& e)
    {
        lg2::info(
            "Could not serialize DisableRedundancy value of {DISABLE}: {ERROR}",
            "DISABLE", disable, "ERROR", e);
    }

    properties.disable_redundancy_override = disable;
    return true;
}

bool RedundancyInterface::set_property(
    [[maybe_unused]] failover_in_progress_t type, bool inProgress)
{
    if (inProgress == failover_in_progress())
    {
        return false;
    }

    try
    {
        data::write(data::key::failoverInProgress, inProgress);
    }
    catch (const std::exception& e)
    {
        lg2::info(
            "Could not serialize FailoverInProgress value of {INPROGRESS}: {ERROR}",
            "INPROGRESS", inProgress, "ERROR", e);
    }

    if (pcieStorage != nullptr)
    {
        try
        {
            pcieStorage->updateFailoverInProgress(inProgress);
        }
        catch (const std::exception& e)
        {
            lg2::warning(
                "Could not write FailoverInProgress to PCIe memory: {ERROR}",
                "ERROR", e);
        }
    }

    properties.failover_in_progress = inProgress;
    return true;
}

bool RedundancyInterface::set_property(
    [[maybe_unused]] reasons_for_no_redundancy_t type,
    const std::vector<ReasonForNoRedundancy>& reasons)
{
    if (reasons == properties.reasons_for_no_redundancy)
    {
        return false;
    }

    // Use the last segment of the string name to trace and save for debug
    std::vector<std::string> names;
    std::ranges::for_each(reasons, [&names](const auto& reason) {
        auto shortName = convertReasonForNoRedundancyToString(reason);
        shortName = shortName.substr(shortName.find_last_of('.') + 1);
        lg2::info("No redundancy because: {DESC}", "DESC", shortName);
        names.push_back(shortName);
    });

    try
    {
        data::write(data::key::noRedReasons, names);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed serializing NoRedundancyReasons: {ERROR}", "ERROR",
                   e);
    }

    properties.reasons_for_no_redundancy = reasons;
    return true;
}

sdbusplus::async::task<> RedundancyInterface::method_call(
    RedundancyInterface::set_redundancy_input_t /* unused */,
    RedundancyInterface::RedundancyInput input, bool value)
{
    manager.setExternalRedundancyInput(input, value);
    co_return;
}

bool RedundancyInterface::set_property(
    [[maybe_unused]] failovers_allowed_t type, bool allowed)
{
    if (allowed == failovers_allowed())
    {
        return false;
    }

    if (pcieStorage != nullptr)
    {
        try
        {
            pcieStorage->updateFailoversAllowed(allowed);
        }
        catch (const std::exception& e)
        {
            lg2::warning(
                "Could not write FailoversAllowed to PCIe memory: {ERROR}",
                "ERROR", e);
        }
    }

    properties.failovers_allowed = allowed;
    return true;
}

} // namespace rbmc
