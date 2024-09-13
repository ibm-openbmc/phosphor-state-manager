/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

Manager::Manager(sdbusplus::async::context& ctx) :
    ctx(ctx),
    redundancyInterface(ctx.get_bus(), RedundancyInterface::instance_path)
{
    ctx.spawn(startup());
}

// clang-tidy currently mangles this into something unreadable
// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::startup()
{
    // TODO: Actually figure out the role
    lg2::info("Setting role to passive");
    redundancyInterface.role(RedundancyInterface::Role::Passive);

    ctx.spawn(doHeartBeat());

    co_return;
}

// clang-tidy currently mangles this into something unreadable
// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::doHeartBeat()
{
    using namespace std::chrono_literals;
    lg2::info("Starting heartbeat");

    while (!ctx.stop_requested())
    {
        redundancyInterface.heartbeat();
        co_await sdbusplus::async::sleep_for(ctx, 1s);
    }

    co_return;
}

} // namespace rbmc
