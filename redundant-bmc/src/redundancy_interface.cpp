// SPDX-License-Identifier: Apache-2.0
#include "redundancy_interface.hpp"

#include "manager.hpp"
#include "persistent_data.hpp"
#include "phosphor-logging/lg2.hpp"

namespace rbmc
{

RedundancyInterface::RedundancyInterface(sdbusplus::async::context& ctx,
                                         Manager& manager) :
    sdbusplus::aserver::xyz::openbmc_project::state::bmc::Redundancy<
        RedundancyInterface>(ctx,
                             RedundancyInterface::Redundancy::instance_path),
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

} // namespace rbmc
