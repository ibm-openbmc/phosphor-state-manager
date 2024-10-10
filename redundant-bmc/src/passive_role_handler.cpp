// SPDX-License-Identifier: Apache-2.0
#include "passive_role_handler.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

constexpr auto bmcPassiveTarget = "obmc-bmc-passive.target";

// NOLINTNEXTLINE
sdbusplus::async::task<> PassiveRoleHandler::start()
{
    try
    {
        // NOLINTNEXTLINE
        co_await services->startUnit(bmcPassiveTarget);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // TODO: error log
        lg2::error("Failed while starting BMC passive target: {ERROR}", "ERROR",
                   e);
    }

    co_return;
}

} // namespace rbmc
