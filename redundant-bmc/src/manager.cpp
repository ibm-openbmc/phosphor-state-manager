/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include "active_role_handler.hpp"
#include "passive_role_handler.hpp"
#include "persistent_data.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<Providers>&& providers) :
    ctx(ctx), redundancyInterface(ctx, *this), providers(std::move(providers))
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
    auto& services = providers->getServices();
    auto& sibling = providers->getSibling();

    co_await sdbusplus::async::execution::when_all(services.init(),
                                                   sibling.init());

    // If we know the role must be passive, set that now,
    // before starting the heartbeat or waiting for the sibling.
    auto passiveRoleInfo = co_await determinePassiveRoleIfRequired();
    if (passiveRoleInfo)
    {
        updateRole(*passiveRoleInfo);
    }

    startHeartbeat();

    if (!passiveRoleInfo)
    {
        if (sibling.isBMCPresent())
        {
            co_await sibling.waitForSiblingUp();

            if (previousRole == Role::Passive)
            {
                co_await sibling.waitForSiblingRole();
            }
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
        handler = std::make_unique<ActiveRoleHandler>(ctx, *providers,
                                                      redundancyInterface);
    }
    else if (redundancyInterface.role() == Role::Passive)
    {
        handler = std::make_unique<PassiveRoleHandler>(ctx, *providers,
                                                       redundancyInterface);
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

void Manager::startHeartbeat()
{
    lg2::info("Starting heartbeat");

    // Emit one now and let the spawn handle the rest.
    redundancyInterface.heartbeat();
    ctx.spawn(doHeartBeat());
}

// clang-tidy currently mangles this into something unreadable
// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::doHeartBeat()
{
    using namespace std::chrono_literals;

    while (!ctx.stop_requested())
    {
        redundancyInterface.heartbeat();
        co_await sdbusplus::async::sleep_for(ctx, 1s);
    }

    co_return;
}

role_determination::RoleInfo Manager::determineRole()
{
    auto& services = providers->getServices();
    auto& sibling = providers->getSibling();

    using namespace role_determination;

    RoleInfo roleInfo{Role::Unknown, RoleReason::unknown};

    try
    {
        // Note:  If these returned nullopts, the algorithm wouldn't use
        //        them anyway because there would be no heartbeat.
        auto siblingRole = sibling.getRole().value_or(Role::Unknown);
        auto siblingProvisioned = sibling.getProvisioned().value_or(false);
        auto siblingPosition = sibling.getPosition().value_or(0xFF);

        role_determination::Input input{
            .bmcPosition = services.getBMCPosition(),
            .previousRole = previousRole,
            .siblingPosition = siblingPosition,
            .siblingRole = siblingRole,
            .siblingHeartbeat = sibling.hasHeartbeat(),
            .siblingProvisioned = siblingProvisioned};

        // If an error case forced it to passive last time, don't use
        // the previous role in the determination so that we don't try
        // to choose the role just because that's what was used last time.
        if (chosePassiveDueToError)
        {
            input.previousRole = Role::Unknown;
        }

        roleInfo = role_determination::determineRole(input);
    }
    catch (const std::exception& e)
    {
        roleInfo.role = Role::Passive;
        roleInfo.reason = RoleReason::exception;
        lg2::error("Exception while determining role: {ERROR}", "ERROR",
                   e.what());
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
    if (!providers->getServices().getProvisioned())
    {
        co_return RoleInfo{Role::Passive, RoleReason::notProvisioned};
    }

    // The sibling service must be up and running.
    if (!providers->getSibling().getInterfacePresent())
    {
        auto state =
            co_await providers->getServices().getUnitState(Sibling::unitName);
        if (state != "active")
        {
            lg2::info("Sibling service state is {STATE}", "STATE", state);
            co_return RoleInfo{Role::Passive,
                               RoleReason::siblingServiceNotRunning};
        }
    }

    co_return std::nullopt;
}
// NOLINTEND

void Manager::updateRole(const role_determination::RoleInfo& roleInfo)
{
    auto reasonDesc =
        role_determination::getRoleReasonDescription(roleInfo.reason);

    lg2::info("Role = {ROLE} due to: {REASON}", "ROLE", roleInfo.role, "REASON",
              reasonDesc);

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

    chosePassiveDueToError = (roleInfo.role == Role::Passive) &&
                             role_determination::isErrorReason(roleInfo.reason);

    try
    {
        data::write(data::key::passiveError, chosePassiveDueToError);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed serializing the role error value of {VALUE}: {ERROR}",
            "VALUE", chosePassiveDueToError, "ERROR", e);
    }

    try
    {
        data::write(data::key::roleReason, reasonDesc);
    }
    catch (const std::exception& e)
    {
        lg2::info("Could not serialize RoleReason value of {REASON}: {ERROR}",
                  "REASON", reasonDesc, "ERROR", e);
    }
}

void Manager::disableRedPropChanged(bool disable)
{
    if (!handler)
    {
        lg2::error(
            "DisableRedundancy property cannot be changed to {VALUE} yet",
            "VALUE", disable);
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    handler->disableRedPropChanged(disable);
}

} // namespace rbmc
