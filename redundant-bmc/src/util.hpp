// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

#include <set>

namespace rbmc::util
{

using RedundancyInput = sdbusplus::common::xyz::openbmc_project::state::bmc::
    Redundancy::RedundancyInput;
using RedundancyInputSet = std::set<RedundancyInput>;

/**
 * @brief Read all saved external redundancy inputs
 *
 * @return RedundancyInputSet - Set of external redundancy inputs, or empty set
 *                              if none exist or on error
 */
RedundancyInputSet readExternalRedundancyInputs();

/**
 * @brief Check if a specific external redundancy input is set
 *
 * @param[in] input - The external input enum value to check
 *
 * @return bool - True if the input is set, false otherwise
 */
bool hasExternalRedundancyInput(RedundancyInput input);

/**
 * @brief Check if any of the specified external redundancy inputs are set
 *
 * @param[in] first - The first external input enum value to check
 * @param[in] rest - Additional external input enum values to check
 *
 * @return bool - True if any input is set, false otherwise
 */
template <typename... Inputs>
bool hasExternalRedundancyInput(RedundancyInput first, Inputs... rest)
{
    return hasExternalRedundancyInput(first) ||
           (... || hasExternalRedundancyInput(rest));
}

/**
 * @brief Add or remove an external redundancy input to/from persistent storage
 *
 * @param[in] input - The external input enum value to add or remove
 * @param[in] set - True to set the input, false to reset it.
 */
void writeExternalRedundancyInput(RedundancyInput input, bool set);

/**
 * @brief Clear all external redundancy inputs from persistent storage
 *
 * @return bool - True if inputs were cleared, false otherwise
 */
bool clearExternalRedundancyInputs();

} // namespace rbmc::util
