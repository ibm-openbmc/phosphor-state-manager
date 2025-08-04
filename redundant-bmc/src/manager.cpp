/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include "active_role_handler.hpp"
#include "passive_role_handler.hpp"
#include "persistent_data.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

const std::string failoverPath =
    std::string{RedundancyInterface::namespace_path::value} + '/' +
    RedundancyInterface::namespace_path::bmc;

Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<Providers>&& providers) :
    sdbusplus::aserver::xyz::openbmc_project::control::Failover<Manager>(
        ctx, failoverPath.c_str()),
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

    try
    {
        // Restore FailoverInProgress on D-Bus
        auto failoverInProgress =
            data::read<bool>(data::key::failoverInProgress).value_or(false);
        if (failoverInProgress)
        {
            lg2::info("Failover was previously in progress");
        }

        redundancyInterface.failover_in_progress(failoverInProgress);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain failover-in-progress: {ERROR}",
                   "ERROR", e);
    }

    // emit the Failover interfaces added signal
    emit_added();

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

    co_await postStartupClearFOInProgress();

    spawnRoleHandler();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::postStartupClearFOInProgress()
{
    if (!redundancyInterface.failover_in_progress())
    {
        co_return;
    }

    // If true, this property is used by both BMCs to determine their roles.
    // This BMC will have already used it.  Check the other BMC already has
    // as well and then it can be set to false and removed from the persistent
    // data.

    lg2::info(
        "Waiting for sibling to get role and then clearing failover in progress");

    co_await providers->getSibling().waitForSiblingRole();

    redundancyInterface.failover_in_progress(false);
    try
    {
        data::remove(data::key::failoverInProgress);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed removing failover-in-progress: {ERROR}", "ERROR", e);
    }
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
        auto siblingFailoverInProgress =
            sibling.getFailoverInProgress().value_or(false);

        role_determination::Input input{
            .bmcPosition = services.getBMCPosition(),
            .previousRole = previousRole,
            .siblingRole = siblingRole,
            .siblingHeartbeat = sibling.hasHeartbeat(),
            .siblingProvisioned = siblingProvisioned,
            .failoverInProgress = redundancyInterface.failover_in_progress(),
            .siblingFailoverInProgress = siblingFailoverInProgress};

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
        if ((state != "active") && (state != "activating"))
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

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::method_call(start_failover_t /* unused */,
                                              const FailoverOptions& options)
{
    if (redundancyInterface.failover_imminent() ||
        redundancyInterface.failover_in_progress())
    {
        lg2::error(
            "Failover not allowed because a failover is already imminent or in progress ");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    if (!handler)
    {
        lg2::error("Failover not allowed because it is too early");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    // Do more in depth checking of system state
    auto reason = co_await handler->getFailoverBlockedReason(options);
    if (reason != fo_blocked::Reason::none)
    {
        lg2::error("Failover is blocked because: {REASON}", "REASON",
                   fo_blocked::getFailoverBlockedDescription(reason));
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    if (redundancyInterface.role() == Role::Passive)
    {
        ctx.spawn(doFailoverFromPassive());
    }
    else
    {
        // Shouldn't get here, would have failed in getFailoverBlockedReason
        lg2::info("StartFailover on active BMC not supported yet");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::doFailoverFromPassive()
{
    lg2::info("Starting failover");

    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    co_await providers->getSyncInterface().disableBackgroundSync();

    // Stop handling as a passive BMC. This BMC no longer needs to
    // watch for any changes from the one we're about to reset.
    handler.reset();

    redundancyInterface.failover_imminent(true);

    // Wait to give the sibling a chance to react to seeing
    // failover imminent before continuing.
    co_await providers->getServices().doFailoverImminentDelay();

    redundancyInterface.failover_imminent(false);

    lg2::info("Setting failover in progress");
    redundancyInterface.failover_in_progress(true);

    // Reset the active so it can come back as passive.
    // If this were to throw, let it restart the app.
    co_await providers->getSiblingReset().toggleReset();

    try
    {
        // Now that its past the reset, save the failover in progress
        // indication in case this BMC is rebooted before the failover
        // is done.
        data::write(data::key::failoverInProgress, true);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed serializing failover-in-progress: {ERROR}", "ERROR",
                   e);
    }

    lg2::info("Claiming active role");
    updateRole(role_determination::RoleInfo{
        Role::Active, role_determination::RoleReason::failover});

    auto* active = new ActiveRoleHandler(ctx, *providers, redundancyInterface);
    handler.reset(active);

    active->clearFailoversAllowedDuringFailover();

    // TODO: Grab local bus.  Not needed if it would be linked
    // into the active target instead.

    co_await active->failoverStartActiveTarget();

    co_await active->failoverWaitForSibling();

    lg2::info("Clearing failover in progress");
    redundancyInterface.failover_in_progress(false);

    try
    {
        data::remove(data::key::failoverInProgress);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed removing failover-in-progress: {ERROR}", "ERROR", e);
    }

    co_await active->failoverDetermineRedundancy();
}

} // namespace rbmc
