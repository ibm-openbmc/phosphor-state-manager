// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"

#include <map>
#include <optional>
#include <string>

namespace rbmc
{

/**
 * @struct GPIOConfig
 *
 * Configuration for a GPIO line
 */
struct GPIOConfig
{
    /**
     * @brief The GPIO line name (e.g., "chassis2-present")
     */
    std::string name;

    /**
     * @brief The polarity of the GPIO line
     *
     * Low means active when the GPIO reads low
     * High means active when the GPIO reads high
     */
    GPIOPolarity polarity;
};

/**
 * @struct BMCConfig
 *
 * Configuration for a single BMC
 */
struct BMCConfig
{
    /**
     * @brief The BMC position
     */
    size_t bmcPos;

    /**
     * @brief GPIO configuration for detecting sibling BMC presence
     */
    GPIOConfig siblingBMCPresentGPIO;
};

/**
 * @struct PCIeConfig
 *
 * Configuration for PCIe storage
 */
struct PCIeConfig
{
    /**
     * @brief The PCIe device path (e.g., "/dev/bmc-device0")
     */
    std::string devicePath;

    /**
     * @brief The offset in the PCIe device for redundancy data as a hex string
     */
    std::string redundancyOffset;
};

/**
 * @struct RedundantBMCConfig
 *
 * Top-level configuration for redundant BMC functionality
 */
struct RedundantBMCConfig
{
    /**
     * @brief GPIO configuration for resetting the sibling BMC
     */
    GPIOConfig siblingBMCResetGPIO;

    /**
     * @brief Configuration for each BMC in the redundant system.
     *        Map key is the BMC position (0-based)
     */
    std::map<size_t, BMCConfig> bmcConfigs;

    /**
     * @brief PCIe storage configuration (optional)
     *
     * If not present, PCIe storage functionality is disabled
     */
    std::optional<PCIeConfig> pcieConfig;
};

} // namespace rbmc
