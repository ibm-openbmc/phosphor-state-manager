// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Control/Failover/common.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>

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

/**
 * @brief Look for the specified failover option in the contents of
 *        the Options parameter from the StartFailover method.
 *
 * @tparam - The type of the option's value.
 * @param[in] option - The option to look for
 * @param[in] options - The options that were passed into StartFailover
 *
 * @return std::optional<type> - The value, or nullopt if not present
 */
template <typename T>
std::optional<T> getFailoverOption(
    sdbusplus::common::xyz::openbmc_project::control::Failover::Options option,
    const FailoverOptions& options)
{
    using Failover = sdbusplus::common::xyz::openbmc_project::control::Failover;
    std::optional<T> value;
    auto it = options.find(Failover::convertOptionsToString(option));
    if (it != options.end())
    {
        if (const T* o = std::get_if<T>(&it->second); o != nullptr)
        {
            value = *o;
        }
    }

    return value;
}

/**
 * @brief Validates the UseRedundancyInput failover option if present
 *
 * @param[in] options - The options passed to StartFailover
 *
 * @return bool - True if option not present or has valid value, false if
 *                invalid
 */
bool validateFailoverRedundancyInput(const FailoverOptions& options);

/**
 * @brief Get a value from an os-release file
 *
 * @param[in] filePath - The path to the os-release file
 * @param[in] key - The key to look for
 *
 * @return std::optional<std::string> - The value, or nullopt if not found
 */
std::optional<std::string> getOSReleaseValue(const std::string& filePath,
                                             const std::string& key);
/**
 * @brief Run a command asynchronously
 *
 * @param[in] ctx - The async context
 * @param[in] cmd - The command to run
 *
 * @return sdbusplus::async::task<int> - The exit code of the command
 */
sdbusplus::async::task<int> runAsyncCmd(sdbusplus::async::context& ctx,
                                        const std::string& cmd);
/**
 * @brief Convert a raw uptime in seconds to a human-readable string
 *        of the form "Xd Xh Xm", omitting leading zero components.
 *        If total is less than one minute, returns "0m".
 *
 * @param[in] uptimeSeconds - Seconds since boot
 *
 * @return std::string - Human-readable uptime string
 */
std::string uptimeToString(uint64_t uptimeSeconds);

} // namespace rbmc::util
