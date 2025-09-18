// SPDX-License-Identifier: Apache-2.0

#include "role_determination.hpp"

namespace rbmc::role_determination
{

RoleInfo determineRole(const Input& input)
{
    RoleInfo result;

    // Must check this before any other sibling fields
    if (!input.siblingAlive)
    {
        result = {Role::Active, RoleReason::siblingNotAlive};
    }

    else if (!input.siblingProvisioned)
    {
        result = {Role::Active, RoleReason::siblingNotProvisioned};
    }

    else if (input.siblingRole == Role::Passive)
    {
        result = {Role::Active, RoleReason::siblingPassive};
    }

    else if (input.siblingRole == Role::Active)
    {
        result = {Role::Passive, RoleReason::siblingActive};
    }

    else if (input.failoverInProgress)
    {
        result = {Role::Active, RoleReason::failoverInProgress};
    }

    else if (input.siblingFailoverInProgress)
    {
        result = {Role::Passive, RoleReason::siblingFailoverInProgress};
    }

    else if (input.previousRole == Role::Active)
    {
        result = {Role::Active, RoleReason::resumePrevious};
    }

    else if (input.previousRole == Role::Passive)
    {
        result = {Role::Passive, RoleReason::resumePrevious};
    }

    else if (input.bmcPosition == 0)
    {
        result = {Role::Active, RoleReason::positionZero};
    }

    else
    {
        result = {Role::Passive, RoleReason::positionNonzero};
    }

    return result;
}

std::string getRoleReasonDescription(RoleReason reason)
{
    using namespace std::string_literals;
    std::string desc;

    switch (reason)
    {
        case RoleReason::unknown:
            desc = "Unknown reason"s;
            break;
        case RoleReason::siblingNotAlive:
            desc = "Sibling not alive"s;
            break;
        case RoleReason::siblingNotProvisioned:
            desc = "Sibling is not provisioned"s;
            break;
        case RoleReason::siblingPassive:
            desc = "Sibling is already passive"s;
            break;
        case RoleReason::siblingActive:
            desc = "Sibling is already active"s;
            break;
        case RoleReason::resumePrevious:
            desc = "Resuming previous role"s;
            break;
        case RoleReason::positionZero:
            desc = "BMC is position 0"s;
            break;
        case RoleReason::positionNonzero:
            desc = "BMC is not position 0"s;
            break;
        case RoleReason::notProvisioned:
            desc = "BMC is not provisioned"s;
            break;
        case RoleReason::siblingServiceNotRunning:
            desc = "Sibling BMC service is not running"s;
            break;
        case RoleReason::failoverInProgress:
            desc = "Newly active from failover";
            break;
        case RoleReason::siblingFailoverInProgress:
            desc = "Sibling was driving a failover";
            break;
        case RoleReason::exception:
            desc = "Exception thrown while determining role"s;
            break;
        case RoleReason::failover:
            desc = "Failover"s;
            break;
        case RoleReason::unknownBMCPosition:
            desc = "Cannot determine BMC position"s;
            break;
    }

    return desc;
}

bool isErrorReason(RoleReason reason)
{
    using enum RoleReason;
    return (reason == notProvisioned) || (reason == siblingServiceNotRunning) ||
           (reason == exception) || (reason == unknownBMCPosition);
}

} // namespace rbmc::role_determination
