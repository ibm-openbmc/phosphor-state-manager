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
};

/**
 * @brief Determines if this BMC should claim the Active or Passive role.
 *
 * @param[in] input  - The structure of inputs
 *
 * @return The role
 */
Role run(const Input& input);

} // namespace role_determination

} // namespace rbmc
