/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include "active_role_handler.hpp"
#include "passive_role_handler.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

const std::chrono::minutes siblingTimeout{6};

Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<Services>&& services,
                 std::unique_ptr<Sibling>&& sibling) :
    ctx(ctx),
    redundancyInterface(ctx.get_bus(), RedundancyInterface::instance_path),
    services(std::move(services)), sibling(std::move(sibling))
{
    ctx.spawn(startup());
}

// clang-tidy currently mangles this into something unreadable
// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::startup()
{
    co_await sibling->init();

    ctx.spawn(doHeartBeat());

    if (sibling->isBMCPresent())
    {
        co_await sibling->waitForSiblingUp(siblingTimeout);
    }

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
    using namespace role_determination;

    RoleInfo roleInfo{Role::Unknown, ErrorCase::noError};

    try
    {
        // Note:  If these returned nullopts, the algorithm wouldn't use
        //        them anyway because there would be no heartbeat.
        auto siblingRole = sibling->getRole().value_or(Role::Unknown);
        auto siblingProvisioned = sibling->getProvisioned().value_or(true);
        auto siblingPosition = sibling->getPosition().value_or(0xFF);

        role_determination::Input input{
            .bmcPosition = services->getBMCPosition(),
            .siblingPosition = siblingPosition,
            .siblingRole = siblingRole,
            .siblingHeartbeat = sibling->hasHeartbeat(),
            .siblingProvisioned = siblingProvisioned};

        roleInfo = role_determination::run(input);
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception while determining, role.  Will have to be "
                   "passive. Error = {ERROR}",
                   "ERROR", e);
        roleInfo.role = Role::Passive;
        roleInfo.error = ErrorCase::internalError;
    }

    if (roleInfo.error != role_determination::ErrorCase::noError)
    {
        // TODO: Create an error log
    }

    return roleInfo.role;
}

} // namespace rbmc
