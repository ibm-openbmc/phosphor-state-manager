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
                                         Manager& manager) :
    sdbusplus::aserver::xyz::openbmc_project::state::bmc::Redundancy<
        RedundancyInterface>(ctx, objectPath.c_str()),
    manager(manager)
{
    try
    {
        disable_redundancy_override_ =
            data::read<bool>(data::key::disableRed).value_or(false);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed trying to obtain previous value of DisableRedundancy");
    }

    try
    {
        failover_in_progress_ =
            data::read<bool>(data::key::failoverInProgress).value_or(false);
        if (failover_in_progress_)
        {
            lg2::info("Failover was previously in progress");
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain failover-in-progress: {ERROR}",
                   "ERROR", e);
    }

    emit_added();
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

    disable_redundancy_override_ = disable;
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

    failover_in_progress_ = inProgress;
    return true;
}

} // namespace rbmc
