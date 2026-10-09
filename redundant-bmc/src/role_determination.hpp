// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

#include <optional>

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
    bool siblingAlive;
    bool failoverInProgress;
    bool siblingFailoverInProgress;
};

/**
 * @brief The reason the role is what it is.
 */
enum class RoleReason
{
    unknown,
    siblingNotAlive,
    siblingPassive,
    siblingActive,
    resumePrevious,
    positionZero,
    positionNonzero,
    notPaired,
    siblingServiceNotRunning,
    exception,
    failover,
    failoverInProgress,
    siblingFailoverInProgress,
    unknownBMCPosition,
    systemInventoryNotAvailable
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

/**
 * @brief Returns true if this BMC should wait for the sibling to
 *        determine its role before finalizing its own.
 *
 * See the implementation for details.
 * The wait is never needed once the sibling has published a role.
 *
 * @param[in] roleInfo     - The role this BMC would claim
 * @param[in] bmcPosition  - This BMC's position
 * @param[in] siblingRole  - The sibling's current role, if known
 *
 * @return true if a wait is needed
 */
bool needDeferToSibling(const RoleInfo& roleInfo,
                        std::optional<size_t> bmcPosition,
                        std::optional<Role> siblingRole);

} // namespace role_determination

} // namespace rbmc
