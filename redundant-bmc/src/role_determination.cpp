// SPDX-License-Identifier: Apache-2.0

#include "role_determination.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc::role_determination
{

RoleInfo run(const Input& input)
{
    // Must check this before any other sibling fields
    if (!input.siblingHeartbeat)
    {
        lg2::info("Role = active due to no sibling heartbeat");
        return {Role::Active, ErrorCase::noError};
    }

    if (input.bmcPosition == input.siblingPosition)
    {
        lg2::error(
            "Role = passive due to both BMC's having the same position {POSITION}",
            "POSITION", input.bmcPosition);
        return {Role::Passive, ErrorCase::samePositions};
    }

    if (!input.siblingProvisioned)
    {
        lg2::info("Role = active due to the sibling not being provisioned");
        return {Role::Active, ErrorCase::noError};
    }

    if (input.siblingRole == Role::Passive)
    {
        lg2::info("Role = active due to the sibling already being passive");
        return {Role::Active, ErrorCase::noError};
    }

    if (input.siblingRole == Role::Active)
    {
        lg2::info("Role = passive due to the sibling already being active");
        return {Role::Passive, ErrorCase::noError};
    }

    if (input.bmcPosition == 0)
    {
        lg2::info("Role = active due to BMC position 0");
        return {Role::Active, ErrorCase::noError};
    }

    lg2::info("Role = passive due to BMC position {POSITION}", "POSITION",
              input.bmcPosition);
    return {Role::Passive, ErrorCase::noError};
}

} // namespace rbmc::role_determination
