/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include "active_role_handler.hpp"
#include "passive_role_handler.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<Services>&& services) :
    ctx(ctx),
    redundancyInterface(ctx.get_bus(), RedundancyInterface::instance_path),
    services(std::move(services))
{
    ctx.spawn(startup());
}

// clang-tidy currently mangles this into something unreadable
// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::startup()
{
    ctx.spawn(doHeartBeat());

    redundancyInterface.role(determineRole());

    spawnRoleHandler();

    co_return;
}

void Manager::spawnRoleHandler()
{
    if (redundancyInterface.role() == Role::Active)
    {
        handler = std::make_unique<ActiveRoleHandler>(ctx, services);
    }
    else if (redundancyInterface.role() == Role::Passive)
    {
        handler = std::make_unique<PassiveRoleHandler>(ctx, services);
    }
    else
    {
        lg2::error(
            "Invalid role {ROLE} found when trying to create role handler",
            "ROLE", redundancyInterface.role());
        throw std::invalid_argument("Invalid role found when spawning handler");
    }

    ctx.spawn(handler->start());
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

Role Manager::determineRole()
{
    Role role{Role::Unknown};

    try
    {
        role_determination::Input input{
            .bmcPosition = services->getBMCPosition()};

        role = role_determination::run(input);
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception while determining, role.  Will have to be "
                   "passive. Error = {ERROR}",
                   "ERROR", e);
        role = Role::Passive;
    }

    lg2::info("Role Determined: {ROLE}", "ROLE", role);

    return role;
}

} // namespace rbmc
