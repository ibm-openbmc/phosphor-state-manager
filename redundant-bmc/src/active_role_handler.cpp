// SPDX-License-Identifier: Apache-2.0
#include "active_role_handler.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

constexpr auto bmcActiveTarget = "obmc-bmc-active.target";
const std::chrono::minutes siblingHBTimeout{5};

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::start()
{
    auto& services = providers.getServices();
    auto& sibling = providers.getSibling();

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

    redMgr.determineAndSetRedundancy();

    // TODO: Create an error if no redundancy

    startSiblingWatches();

    co_return;
}

void ActiveRoleHandler::siblingStateChange(BMCState state)
{
    if (state == BMCState::Quiesced)
    {
        lg2::error("Sibling BMC went to Quiesce, disabling redundancy");
        redMgr.determineAndSetRedundancy();
    }
}

void ActiveRoleHandler::siblingHBChange(bool hb)
{
    if (hb)
    {
        siblingHBTimer.stop();
        ctx.spawn(siblingHBStarted());
    }
    else
    {
        lg2::info("Sibling BMC heartbeat lost");
        if (redundancyInterface.redundancy_enabled())
        {
            lg2::info(
                "Disabling redundancy in {TIME} minutes if sibling heartbeat doesn't recover",
                "TIME", siblingHBTimeout.count());
            siblingHBTimer.start(siblingHBTimeout);
        }
    }
}

void ActiveRoleHandler::siblingHBCritical()
{
    lg2::error("Sibling heartbeat timer expired, disabling redundancy");
    redMgr.determineAndSetRedundancy();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ActiveRoleHandler::siblingHBStarted()
{
    lg2::info("Passive BMC heartbeat started");

    stopSiblingWatches();

    auto& sibling = providers.getSibling();
    co_await sdbusplus::async::execution::when_all(
        sibling.waitForSiblingRole(), sibling.waitForBMCSteadyState());

    lg2::info("Attempting to enable redundancy now that sibling is back");
    redMgr.determineAndSetRedundancy();

    // TODO: full sync, etc

    startSiblingWatches();

    co_return;
}

} // namespace rbmc
