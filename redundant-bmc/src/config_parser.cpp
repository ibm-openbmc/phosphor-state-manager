// SPDX-License-Identifier: Apache-2.0

#include "config_parser.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <stdexcept>

namespace rbmc::config_parser
{

namespace
{
/**
 * @brief Parse GPIO polarity string to enum
 *
 * @param[in] polarity - The polarity string to parse
 *
 * @return GpioPolarity enum value
 * @throws std::runtime_error if polarity is not "low" or "high"
 */
GPIOPolarity parsePolarity(const std::string& polarity)
{
    if (polarity == "low")
    {
        return GPIOPolarity::low;
    }
    else if (polarity == "high")
    {
        return GPIOPolarity::high;
    }
    else
    {
        throw std::runtime_error("Invalid GPIO polarity '" + polarity +
                                 "'. Must be 'low' or 'high'");
    }
}

/**
 * @brief Parse a GPIO configuration from JSON
 *
 * @param[in] gpioJSON - The JSON object containing GPIO config
 * @param[in] fieldName - The name of the GPIO field (for error messages)
 *
 * @return GPIOConfig object
 * @throws std::runtime_error if required fields are missing or invalid
 */
GPIOConfig parseGPIOConfig(const nlohmann::json& gpioJSON,
                           const std::string& fieldName)
{
    auto nameIt = gpioJSON.find("name");
    if (nameIt == gpioJSON.end())
    {
        throw std::runtime_error(
            fieldName + " config missing required 'name' field");
    }

    auto polarityIt = gpioJSON.find("polarity");
    if (polarityIt == gpioJSON.end())
    {
        throw std::runtime_error(
            fieldName + " config missing required 'polarity' field");
    }

    GPIOConfig config;
    config.name = nameIt->get<std::string>();
    config.polarity = parsePolarity(polarityIt->get<std::string>());
    return config;
}

/**
 * @brief Parse a single BMC configuration from JSON
 *
 * @param[in] bmcJSON - The JSON object containing BMC config
 *
 * @return BMCConfig object
 * @throws std::runtime_error if required fields are missing or invalid
 */
BMCConfig parseBMCConfig(const nlohmann::json& bmcJSON)
{
    BMCConfig config;

    auto bmcPosIt = bmcJSON.find("bmc_pos");
    if (bmcPosIt == bmcJSON.end())
    {
        throw std::runtime_error("BMC config missing required 'bmc_pos' field");
    }
    config.bmcPos = bmcPosIt->get<size_t>();

    auto gpioIt = bmcJSON.find("sibling_bmc_present_gpio");
    if (gpioIt == bmcJSON.end())
    {
        throw std::runtime_error(
            "BMC config missing required 'sibling_bmc_present_gpio' field");
    }
    config.siblingBMCPresentGPIO =
        parseGPIOConfig(*gpioIt, "sibling_bmc_present_gpio");

    return config;
}

/**
 * @brief Parse BMC configurations array from JSON
 *
 * @param[in] jsonData - The root JSON object
 *
 * @return Map of BMC position to BMCConfig
 * @throws std::runtime_error if array is invalid or configs are malformed
 */
std::map<size_t, BMCConfig> parseBMCConfigs(const nlohmann::json& jsonData)
{
    std::map<size_t, BMCConfig> configs;

    auto bmcConfigsIt = jsonData.find("bmc_configs");
    if (bmcConfigsIt == jsonData.end())
    {
        return configs;
    }

    const auto& bmcConfigsJSON = *bmcConfigsIt;
    if (!bmcConfigsJSON.is_array())
    {
        throw std::runtime_error("'bmc_configs' must be an array");
    }

    for (const auto& bmcJSON : bmcConfigsJSON)
    {
        auto bmcConfig = parseBMCConfig(bmcJSON);
        auto [it, inserted] =
            configs.emplace(bmcConfig.bmcPos, std::move(bmcConfig));
        if (!inserted)
        {
            throw std::runtime_error(
                "Duplicate bmc_pos " + std::to_string(it->first) +
                " found in bmc_configs array");
        }
    }

    return configs;
}

/**
 * @brief Parse PCIe configuration from JSON
 *
 * @param[in] jsonData - The root JSON object
 *
 * @return Optional PCIeConfig object (empty if pcie_config not present)
 * @throws std::runtime_error if required fields are missing or invalid
 */
std::optional<PCIeConfig> parsePCIeConfig(const nlohmann::json& jsonData)
{
    auto pcieConfigIt = jsonData.find("pcie_config");
    if (pcieConfigIt == jsonData.end())
    {
        // pcie_config is optional
        return std::nullopt;
    }

    const auto& pcieJSON = *pcieConfigIt;

    auto devicePathIt = pcieJSON.find("device_path");
    if (devicePathIt == pcieJSON.end())
    {
        throw std::runtime_error(
            "pcie_config missing required 'device_path' field");
    }

    auto redundancyOffsetIt = pcieJSON.find("redundancy_offset");
    if (redundancyOffsetIt == pcieJSON.end())
    {
        throw std::runtime_error(
            "pcie_config missing required 'redundancy_offset' field");
    }

    PCIeConfig config;
    config.devicePath = devicePathIt->get<std::string>();
    config.redundancyOffset = redundancyOffsetIt->get<std::string>();
    return config;
}

} // anonymous namespace

RedundantBMCConfig parse(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("Config file not found: " + path.string());
    }

    try
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error(
                "Failed to open config file: " + path.string());
        }

        nlohmann::json jsonData;
        file >> jsonData;

        RedundantBMCConfig config;

        auto resetGpioIt = jsonData.find("sibling_bmc_reset_gpio");
        if (resetGpioIt == jsonData.end())
        {
            throw std::runtime_error(
                "Config file missing required 'sibling_bmc_reset_gpio' field");
        }

        config.siblingBMCResetGPIO =
            parseGPIOConfig(*resetGpioIt, "sibling_bmc_reset_gpio");

        config.bmcConfigs = parseBMCConfigs(jsonData);
        config.pcieConfig = parsePCIeConfig(jsonData);

        return config;
    }
    catch (const nlohmann::json::exception& e)
    {
        throw std::runtime_error("JSON parsing error in config file " +
                                 path.string() + ": " + e.what());
    }
}

RedundantBMCConfig readConfig()
{
    constexpr auto configPath =
        "/usr/share/phosphor-state-manager/redundant-bmc/config.json";
    return parse(configPath);
}

} // namespace rbmc::config_parser
