/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include "active_role_handler.hpp"
#include "passive_role_handler.hpp"
#include "persistent_data.hpp"

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
    try
    {
        previousRole =
            data::read<Role>(data::key::role).value_or(Role::Unknown);
        lg2::info("Previous role was {ROLE}", "ROLE", previousRole);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain previous role: {ERROR}", "ERROR",
                   e);
    }

    try
    {
        chosePassiveDueToError =
            data::read<bool>(data::key::passiveError).value_or(false);
        if (chosePassiveDueToError)
        {
            lg2::info("Was previously passive due to error");
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain previous role error: {ERROR}",
                   "ERROR", e);
    }

    ctx.spawn(startup());
}

// clang-tidy currently mangles this into something unreadable
// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::startup()
{
    co_await sibling->init();

    // If we know the role must be passive, set that now,
    // before starting the heartbeat or waiting for the sibling.
    auto passiveRoleInfo = co_await determinePassiveRoleIfRequired();
    if (passiveRoleInfo)
    {
        updateRole(*passiveRoleInfo);
    }

    ctx.spawn(doHeartBeat());

    if (!passiveRoleInfo)
    {
        if (sibling->isBMCPresent())
        {
            co_await sibling->waitForSiblingUp(siblingTimeout);
        }

        updateRole(determineRole());
    }

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

role_determination::RoleInfo Manager::determineRole()
{
    using namespace role_determination;

    RoleInfo roleInfo{Role::Unknown, ErrorCase::noError};

    try
    {
        // Note:  If these returned nullopts, the algorithm wouldn't use
        //        them anyway because there would be no heartbeat.
        auto siblingRole = sibling->getRole().value_or(Role::Unknown);
        auto siblingProvisioned = sibling->getProvisioned().value_or(false);
        auto siblingPosition = sibling->getPosition().value_or(0xFF);

        role_determination::Input input{
            .bmcPosition = services->getBMCPosition(),
            .previousRole = previousRole,
            .siblingPosition = siblingPosition,
            .siblingRole = siblingRole,
            .siblingHeartbeat = sibling->hasHeartbeat(),
            .siblingProvisioned = siblingProvisioned};

        // If an error case forced it to passive last time, don't use
        // the previous role in the determination so that we don't try
        // to choose the role just because that's what was used last time.
        if (chosePassiveDueToError)
        {
            input.previousRole = Role::Unknown;
        }

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

    // TODO, probably: Create an error log if passive due to an error

    return roleInfo;
}

// clang-tidy appears to get confused on some code down in stdexec
// in this function.  Hopefully a future version of clang will fix it.
// NOLINTBEGIN
sdbusplus::async::task<std::optional<role_determination::RoleInfo>>
    Manager::determinePassiveRoleIfRequired()
{
    using namespace role_determination;

    // An unprovisioned BMC cannot be active.
    if (!services->getProvisioned())
    {
        lg2::info("Role = passive because BMC not provisioned");
        co_return RoleInfo{Role::Passive, ErrorCase::notProvisioned};
    }

    // The sibling service must be up and running.
    if (!sibling->getInterfacePresent())
    {
        auto state = co_await services->getUnitState(Sibling::unitName);
        if (state != "active")
        {
            lg2::info("Role = passive because sibling BMC service not running");
            co_return RoleInfo{Role::Passive, ErrorCase::noSiblingService};
        }
    }

    co_return std::nullopt;
}
// NOLINTEND

void Manager::updateRole(const role_determination::RoleInfo& roleInfo)
{
    redundancyInterface.role(roleInfo.role);

    try
    {
        data::write(data::key::role, roleInfo.role);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed serializing the role value of {ROLE}: {ERROR}",
                   "ROLE", roleInfo.role, "ERROR", e);
    }

    chosePassiveDueToError =
        (roleInfo.role == Role::Passive) &&
        (roleInfo.error != role_determination::ErrorCase::noError);

    try
    {
        data::write(data::key::passiveError, chosePassiveDueToError);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed serializing the role error value of {VALUE}: {ERROR}",
            "VALUE", roleInfo.error, "ERROR", e);
    }
}

} // namespace rbmc
