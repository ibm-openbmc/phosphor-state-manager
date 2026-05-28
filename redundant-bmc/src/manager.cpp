/* SPDX-License-Identifier: Apache-2.0 */
#include "manager.hpp"

#include "active_role_handler.hpp"
#include "error_data.hpp"
#include "errors.hpp"
#include "passive_role_handler.hpp"
#include "persistent_data.hpp"
#include "util.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace rbmc
{

const std::string failoverPath =
    std::string{RedundancyInterface::namespace_path::value} + '/' +
    RedundancyInterface::namespace_path::bmc;

Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<Providers>&& providers,
                 std::chrono::milliseconds heartbeatInterval) :
    sdbusplus::aserver::xyz::openbmc_project::control::Failover<Manager>(
        ctx, failoverPath.c_str()),
    ctx(ctx), redundancyInterface(ctx, *this), providers(std::move(providers)),
    heartbeatInterval(heartbeatInterval)
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
            lg2::warning("BMC was previously passive due to error");
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed trying to obtain previous role error: {ERROR}",
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

    co_await services.waitForSelfPairing();

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

        updateRole(co_await determineRole());
    }

    if (chosePassiveDueToError)
    {
        using namespace errors;
        AdditionalData data{
            {"RoleReasonVal", std::to_string(std::to_underlying(roleReason))}};
        addDefaultData(redundancyInterface, *providers, data);

        co_await services.logError(error_msg::bmcIsPassiveDueToError,
                                   Level::Error, data);
    }

    co_await postStartupClearFOInProgress();

    spawnRoleHandler();

    if (!services.getPaired())
    {
        setupPairedWatch();
    }
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
    // as well and then it can be set to false.

    lg2::info(
        "Waiting for sibling to get role and then clearing failover in progress");

    co_await providers->getSibling().waitForSiblingRole();

    redundancyInterface.failover_in_progress(false);
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

sdbusplus::async::task<> Manager::doHeartBeat()
{
    while (!ctx.stop_requested())
    {
        redundancyInterface.heartbeat();
        co_await sdbusplus::async::sleep_for(ctx, heartbeatInterval);
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<role_determination::RoleInfo> Manager::determineRole()
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
        auto siblingFailoverInProgress =
            sibling.getFailoverInProgress().value_or(false);

        // determineRole() doesn't support an empty BMC position because
        // it should have been caught in determinePassiveRoleIfRequired.
        auto bmcPos = services.getBMCPosition();
        if (!bmcPos.has_value())
        {
            lg2::error("determineRole: No BMC position");
            co_return RoleInfo{Role::Passive, RoleReason::unknownBMCPosition};
        }

        role_determination::Input input{
            .bmcPosition = bmcPos.value(),
            .previousRole = previousRole,
            .siblingRole = siblingRole,
            .siblingAlive = sibling.alive(),
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

    co_return roleInfo;
}

// clang-tidy appears to get confused on some code down in stdexec
// in this function.  Hopefully a future version of clang will fix it.
// NOLINTBEGIN
sdbusplus::async::task<std::optional<role_determination::RoleInfo>>
    Manager::determinePassiveRoleIfRequired()
{
    using namespace role_determination;

    // An unpaired BMC cannot be active.
    if (!providers->getServices().getPaired())
    {
        co_return RoleInfo{Role::Passive, RoleReason::notPaired};
    }

    // A BMC with no position cannot be active.
    auto bmcPos = providers->getServices().getBMCPosition();
    if (!bmcPos.has_value())
    {
        co_return RoleInfo{Role::Passive, RoleReason::unknownBMCPosition};
    }

    // A BMC with a failed system inventory status cannot be active.
    if (!co_await providers->getServices().checkSystemInventoryStatus())
    {
        co_return RoleInfo{Role::Passive,
                           RoleReason::systemInventoryNotAvailable};
    }

    // If the sibling service isn't on D-Bus, the BMC can't be active.
    if (providers->getSibling().getServiceName().empty())
    {
        lg2::error("Sibling service is not running");
        co_return RoleInfo{Role::Passive, RoleReason::siblingServiceNotRunning};
    }

    co_return std::nullopt;
}
// NOLINTEND

void Manager::updateRole(const role_determination::RoleInfo& roleInfo)
{
    roleReason = roleInfo.reason;

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
            "Failed serializing the PassiveDueToError value of {VALUE}: {ERROR}",
            "VALUE", chosePassiveDueToError, "ERROR", e);
    }

    try
    {
        data::write(data::key::roleReason, reasonDesc);
    }
    catch (const std::exception& e)
    {
        lg2::error("Could not serialize RoleReason value of {REASON}: {ERROR}",
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

void Manager::setExternalRedundancyInput(
    RedundancyInterface::RedundancyInput input, bool value)
{
    lg2::info("SetRedundancyInput called: {INPUT}={VALUE}", "INPUT", input,
              "VALUE", value);

    if (redundancyInterface.role() != Role::Active)
    {
        lg2::error("SetRedundancyInput can only be called on active BMC");
        throw sdbusplus::xyz::openbmc_project::Common::Error::NotAllowed();
    }

    if (!handler)
    {
        lg2::error(
            "SetRedundancyInput cannot be called yet, role handler not initialized");
        throw sdbusplus::xyz::openbmc_project::Common::Error::NotAllowed();
    }

    util::writeExternalRedundancyInput(input, value);

    handler->externalRedundancyInputChanged();
}

sdbusplus::async::task<fo_blocked::Reason> Manager::validateFailoverRequest(
    const FailoverOptions& options)
{
    if (!handler)
    {
        co_return fo_blocked::Reason::tooEarly;
    }

    co_return co_await handler->getFailoverBlockedReason(options);
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::method_call(start_failover_t /* unused */,
                                              Requester requester,
                                              const FailoverOptions& options)
{
    auto reqString = convertRequesterToString(requester);
    if (reqString.contains('.'))
    {
        reqString = reqString.substr(reqString.find_last_of('.') + 1);
    }

    lg2::info("Failover requester is {REQUESTER}", "REQUESTER", reqString);

    errors::AdditionalData data{
        {"FORequester", reqString},
        {"FORequesterVal", std::to_string(std::to_underlying(requester))}};

    errors::addDefaultData(redundancyInterface, *providers, data);
    errors::addFailoverOptsToData(options, data);

    auto blockedReason = co_await validateFailoverRequest(options);

    if (blockedReason != fo_blocked::Reason::none)
    {
        auto blockedReasonDesc =
            fo_blocked::getFailoverBlockedDescription(blockedReason);

        lg2::error("StartFailover failed because: {REASON}", "REASON",
                   blockedReasonDesc);

        data.emplace("FOBlockedReason", blockedReasonDesc);
        data.emplace("FOBlockedReasonVal",
                     std::to_string(std::to_underlying(blockedReason)));

        auto sev = (requester == Requester::Tool) ? errors::Level::Informational
                                                  : errors::Level::Warning;

        co_await providers->getServices().logError(
            errors::error_msg::failoverBlocked, sev, data);

        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }

    if (redundancyInterface.role() == Role::Passive)
    {
        co_await providers->getServices().logError(
            errors::error_msg::failoverStarted, errors::Level::Informational,
            data);

        ctx.spawn(doFailoverFromPassive(requester));
    }
    else
    {
        // Shouldn't get here, would have failed in validateFailoverRequest
        lg2::error("StartFailover on active BMC not supported yet");
        throw sdbusplus::xyz::openbmc_project::Common::Error::Unavailable();
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::doFailoverFromPassive(Requester requester)
{
    lg2::info("Starting failover");
    time_t now = time(nullptr);

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

    // Reset the active so it can come back as passive.
    // If this were to throw, let it restart the app.
    co_await providers->getSiblingReset().toggleReset();

    // Needs to be set after the point of no-return so if this BMC
    // is rebooted it could handle it.
    lg2::info("Setting failover in progress");
    redundancyInterface.failover_in_progress(true);

    // After the point of no return, log it
    data::logFailover(providers->getServices().getPersistentDataPath(),
                      providers->getServices().getBMCPosition().value_or(0xFF),
                      requester, now);

    lg2::info("Claiming active role");
    updateRole(role_determination::RoleInfo{
        Role::Active, role_determination::RoleReason::failover});

    auto* active = new ActiveRoleHandler(ctx, *providers, redundancyInterface);
    handler.reset(active);

    active->clearFailoversAllowedDuringFailover();

    co_await active->failoverStartActiveTarget();

    co_await active->failoverWaitForSibling();

    lg2::info("Clearing failover in progress");
    redundancyInterface.failover_in_progress(false);

    co_await active->failoverDetermineRedundancy();
}

void Manager::setupPairedWatch()
{
    providers->getServices().addPairedCallback(
        Role::Passive, std::bind_front(&Manager::pairedChangeHandler, this));
}

void Manager::pairedChangeHandler(bool paired)
{
    ctx.spawn(handlePairedChange(paired));
}

sdbusplus::async::task<> Manager::handlePairedChange(bool paired)
{
    if (!paired)
    {
        co_return;
    }

    // Doublecheck the BMC is passive.  It should be.
    if (redundancyInterface.role() != Role::Passive)
    {
        lg2::warning(
            "Paired just changed to true but BMC not already passive?");
        providers->getServices().removePairedCallback(Role::Passive);
        co_return;
    }

    // If being passive isn't still required, clear the
    // reasons_for_no_redundancy property which will let the other BMC
    // know this BMC can be active again.

    auto passiveRoleInfo = co_await determinePassiveRoleIfRequired();

    if (!passiveRoleInfo.has_value())
    {
        lg2::info("This BMC is no longer required to be passive");

        // Note: Leave chosePassiveDueToError so the next reboot still sees it.

        redundancyInterface.reasons_for_no_redundancy({});

        // No need for future callbacks.
        providers->getServices().removePairedCallback(Role::Passive);
    }
    else
    {
        lg2::info(
            "After paired property change to true, BMC still required to be passive: {REASON}",
            "REASON",
            role_determination::getRoleReasonDescription(
                passiveRoleInfo->reason));
    }
}

} // namespace rbmc
