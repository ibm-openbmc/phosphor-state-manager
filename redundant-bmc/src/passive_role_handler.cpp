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

    // Mirror the active BMC's RedundancyEnabled value
    auto sibRedEnabled = sibling.getRedundancyEnabled();
    if (sibRedEnabled)
    {
        siblingRedEnabledHandler(sibRedEnabled.value());
    }

    // Also register for changes
    sibling.addRedundancyEnabledCallback(Role::Passive, [this](bool enabled) {
        siblingRedEnabledHandler(enabled);
    });
}

void PassiveRoleHandler::siblingRedEnabledHandler(bool enable)
{
    // If the sibling is Active, mirror the property on this BMC.
    if (providers.getSibling().getRole().value_or(Role::Unknown) ==
        Role::Active)
    {
        redundancyInterface.redundancy_enabled(enable);
    }
}

void PassiveRoleHandler::disableRedPropChanged(bool /*disable*/)
{
    lg2::error("Cannot modify DisableRedundancy property on passive BMC");
    throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
}

} // namespace rbmc
