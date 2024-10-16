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
    size_t siblingPosition;
    Role siblingRole;
    bool siblingHeartbeat;
    bool siblingProvisioned;
};

/**
 * @brief Role determination error cases.
 */
enum class ErrorCase
{
    noError,
    internalError,
    samePositions
};

/**
 * @brief The role and the error reason returned from run()
 */
struct RoleInfo
{
    Role role;
    ErrorCase error;

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
RoleInfo run(const Input& input);

} // namespace role_determination

} // namespace rbmc
