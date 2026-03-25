// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "config_data.hpp"

#include <filesystem>

namespace rbmc::config_parser
{

/**
 * @brief Parse the redundant BMC configuration file from JSON format
 *
 * @param[in] path - Path to the JSON file
 *
 * @return The parsed configuration
 *
 * @throws std::runtime_error if the file doesn't exist or cannot be parsed
 * @throws nlohmann::json::exception if JSON parsing fails
 */
RedundantBMCConfig parse(const std::filesystem::path& path);

/**
 * @brief Reads the config from file.
 *
 * Uses /usr/share/phosphor-state-manager/redundant-bmc/config.json
 *
 * @return The parsed configuration
 */
RedundantBMCConfig readConfig();

} // namespace rbmc::config_parser
