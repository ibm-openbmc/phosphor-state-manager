// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

namespace rbmc
{

using Role =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy::Role;

namespace role_determination
{

/**
 * @brief Inputs to the role determination function.
 */
struct Input
{
    size_t bmcPosition;
    Role previousRole;
    Role siblingRole;
    bool siblingHeartbeat;
    bool siblingProvisioned;
};

/**
 * @brief The reason the role is what it is.
 */
enum class RoleReason
{
    unknown,
    noSiblingHeartbeat,
    siblingNotProvisioned,
    siblingPassive,
    siblingActive,
    resumePrevious,
    positionZero,
    positionNonzero,
    notProvisioned,
    siblingServiceNotRunning,
    exception
};

/**
 * @brief The role and the reason returned from determineRole()
 */
struct RoleInfo
{
    Role role;
    RoleReason reason;

    // use the default <, ==, > operators for compares
    auto operator<=>(const RoleInfo&) const = default;
};

/**
 * @brief Determines if this BMC should claim the Active or Passive role.
 *
 * @param[in] input  - The structure of inputs
 *
 * @return The role and error case
 */
RoleInfo determineRole(const Input& input);

/**
 * @brief Return the string description of the reason
 *
 * @return The human readable description.
 */
std::string getRoleReasonDescription(RoleReason reason);

/**
 * @brief If the reason is an error case that requires the
 *        BMC to be passive.
 */
bool isErrorReason(RoleReason reason);

} // namespace role_determination

} // namespace rbmc
