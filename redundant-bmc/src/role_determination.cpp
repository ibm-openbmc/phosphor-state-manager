// SPDX-License-Identifier: Apache-2.0

#include "role_determination.hpp"

namespace rbmc::role_determination
{

RoleInfo determineRole(const Input& input)
{
    RoleInfo result;

    // Must check this before any other sibling fields
    if (!input.siblingHeartbeat)
    {
        result = {Role::Active, RoleReason::noSiblingHeartbeat};
    }

    else if (input.bmcPosition == input.siblingPosition)
    {
        result = {Role::Passive, RoleReason::samePositions};
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
        case RoleReason::noSiblingHeartbeat:
            desc = "No sibling heartbeat"s;
            break;
        case RoleReason::samePositions:
            desc = "Both BMCs have the same position"s;
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
        case RoleReason::exception:
            desc = "Exception thrown while determining role"s;
            break;
    }

    return desc;
}

bool isErrorReason(RoleReason reason)
{
    using enum RoleReason;
    return (reason == samePositions) || (reason == notProvisioned) ||
           (reason == siblingServiceNotRunning) || (reason == exception);
}

} // namespace rbmc::role_determination
