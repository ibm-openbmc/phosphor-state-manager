// SPDX-License-Identifier: Apache-2.0

#include "config_parser.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

#include <gtest/gtest.h>

using namespace rbmc::config_parser;

class ConfigParserTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        std::string templatePath =
            (std::filesystem::temp_directory_path() / "rbmc_config_test_XXXXXX")
                .string();
        testDir = mkdtemp(templatePath.data());
    }

    void TearDown() override
    {
        std::filesystem::remove_all(testDir);
    }

    void writeConfigFile(std::string_view filename, const std::string& content)
    {
        std::ofstream file(testDir / filename);
        file << content;
    }

    std::filesystem::path testDir;
};

TEST_F(ConfigParserTest, ParseValidConfig)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0,
                "sibling_bmc_present_gpio": {
                    "name": "chassis2-present",
                    "polarity": "low"
                }
            },
            {
                "bmc_pos": 1,
                "sibling_bmc_present_gpio": {
                    "name": "chassis1-present",
                    "polarity": "high"
                }
            }
        ],
        "pcie_config": {
            "device_path": "/dev/bmc-device0",
            "redundancy_offset": "66060288"
        }
    })";

    writeConfigFile("valid_config.json", config);

    auto result = parse(testDir / "valid_config.json");

    EXPECT_EQ(result.bmcConfigs.size(), 2);

    // Check reset GPIO
    EXPECT_EQ(result.siblingBMCResetGPIO.name, "sibling-bmc-reset-n");
    EXPECT_EQ(result.siblingBMCResetGPIO.polarity, rbmc::GPIOPolarity::low);

    // Check first BMC config
    ASSERT_TRUE(result.bmcConfigs.contains(0));
    const auto& bmc0 = result.bmcConfigs.at(0);
    EXPECT_EQ(bmc0.bmcPos, 0);
    EXPECT_EQ(bmc0.siblingBMCPresentGPIO.name, "chassis2-present");
    EXPECT_EQ(bmc0.siblingBMCPresentGPIO.polarity, rbmc::GPIOPolarity::low);

    // Check second BMC config
    ASSERT_TRUE(result.bmcConfigs.contains(1));
    const auto& bmc1 = result.bmcConfigs.at(1);
    EXPECT_EQ(bmc1.bmcPos, 1);
    EXPECT_EQ(bmc1.siblingBMCPresentGPIO.name, "chassis1-present");
    EXPECT_EQ(bmc1.siblingBMCPresentGPIO.polarity, rbmc::GPIOPolarity::high);
}

TEST_F(ConfigParserTest, ParseOptionalBMCConfigs)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "pcie_config": {
            "device_path": "/dev/bmc-device0",
            "redundancy_offset": "66060288"
        }
    })";

    writeConfigFile("optional_bmc_configs.json", config);

    auto result = parse(testDir / "optional_bmc_configs.json");

    EXPECT_EQ(result.bmcConfigs.size(), 0);
}

TEST_F(ConfigParserTest, ParseEmptyBMCConfigsArray)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [],
        "pcie_config": {
            "device_path": "/dev/bmc-device0",
            "redundancy_offset": "66060288"
        }
    })";

    writeConfigFile("empty_bmc_configs.json", config);

    auto result = parse(testDir / "empty_bmc_configs.json");

    EXPECT_EQ(result.bmcConfigs.size(), 0);
}

TEST_F(ConfigParserTest, ParseMissingPresentGPIO)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset",
            "polarity": "high"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0
            }
        ]
    })";

    writeConfigFile("no_present_gpio_config.json", config);

    // sibling_bmc_present_gpio is now required
    EXPECT_THROW(parse(testDir / "no_present_gpio_config.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseNonExistentFile)
{
    EXPECT_THROW(parse(testDir / "nonexistent.json"), std::runtime_error);
}

TEST_F(ConfigParserTest, ParseInvalidJSON)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0
            }
    )"; // missing closing brace

    writeConfigFile("invalid.json", config);

    EXPECT_THROW(parse(testDir / "invalid.json"), std::runtime_error);
}

TEST_F(ConfigParserTest, ParseMissingResetGPIO)
{
    const std::string config = R"({
        "bmc_configs": [
            {
                "bmc_pos": 0
            }
        ]
    })";

    writeConfigFile("missing_reset_gpio.json", config);

    EXPECT_THROW(parse(testDir / "missing_reset_gpio.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseMissingBMCPos)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "sibling_bmc_present_gpio": {
                    "name": "test-gpio",
                    "polarity": "low"
                }
            }
        ]
    })";

    writeConfigFile("missing_bmc_pos.json", config);

    EXPECT_THROW(parse(testDir / "missing_bmc_pos.json"), std::runtime_error);
}

TEST_F(ConfigParserTest, ParseInvalidResetGPIOPolarity)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset",
            "polarity": "invalid"
        }
    })";

    writeConfigFile("invalid_reset_polarity.json", config);

    EXPECT_THROW(parse(testDir / "invalid_reset_polarity.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseInvalidPresentGPIOPolarity)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0,
                "sibling_bmc_present_gpio": {
                    "name": "test-gpio",
                    "polarity": "invalid"
                }
            }
        ]
    })";

    writeConfigFile("invalid_present_polarity.json", config);

    EXPECT_THROW(parse(testDir / "invalid_present_polarity.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseMissingResetGPIOName)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "polarity": "low"
        }
    })";

    writeConfigFile("missing_reset_gpio_name.json", config);

    EXPECT_THROW(parse(testDir / "missing_reset_gpio_name.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseMissingResetGPIOPolarity)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset"
        }
    })";

    writeConfigFile("missing_reset_gpio_polarity.json", config);

    EXPECT_THROW(parse(testDir / "missing_reset_gpio_polarity.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseMissingPresentGPIOName)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0,
                "sibling_bmc_present_gpio": {
                    "polarity": "low"
                }
            }
        ]
    })";

    writeConfigFile("missing_present_gpio_name.json", config);

    EXPECT_THROW(parse(testDir / "missing_present_gpio_name.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseMissingPresentGPIOPolarity)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0,
                "sibling_bmc_present_gpio": {
                    "name": "test-gpio"
                }
            }
        ]
    })";

    writeConfigFile("missing_present_gpio_polarity.json", config);

    EXPECT_THROW(parse(testDir / "missing_present_gpio_polarity.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseBMCConfigsNotArray)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": "not an array"
    })";

    writeConfigFile("bmc_configs_not_array.json", config);

    EXPECT_THROW(parse(testDir / "bmc_configs_not_array.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParseDuplicateBMCPosition)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [
            {
                "bmc_pos": 0,
                "sibling_bmc_present_gpio": {
                    "name": "gpio1",
                    "polarity": "low"
                }
            },
            {
                "bmc_pos": 0,
                "sibling_bmc_present_gpio": {
                    "name": "gpio2",
                    "polarity": "high"
                }
            }
        ]
    })";

    writeConfigFile("duplicate_bmc_pos.json", config);

    // Duplicate bmc_pos should cause parsing to fail
    EXPECT_THROW(parse(testDir / "duplicate_bmc_pos.json"), std::runtime_error);
}

TEST_F(ConfigParserTest, ParsePCIeConfig)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity":  "low"
        },
        "bmc_configs": [],
        "pcie_config": {
            "device_path": "/dev/bmc-device0",
            "redundancy_offset": "0x3F00000"
        }
    })";

    writeConfigFile("pcie_config.json", config);

    auto result = parse(testDir / "pcie_config.json");

    ASSERT_TRUE(result.pcieConfig.has_value());
    EXPECT_EQ(result.pcieConfig->devicePath, "/dev/bmc-device0");
    EXPECT_EQ(result.pcieConfig->redundancyOffset, "0x3F00000");
}

TEST_F(ConfigParserTest, ParseOptionalPCIeConfig)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": []
    })";

    writeConfigFile("no_pcie_config.json", config);

    auto result = parse(testDir / "no_pcie_config.json");

    EXPECT_FALSE(result.pcieConfig.has_value());
}

TEST_F(ConfigParserTest, ParsePCIeConfigMissingDevicePath)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [],
        "pcie_config": {
            "redundancy_offset": "0x3F00000"
        }
    })";

    writeConfigFile("pcie_missing_device_path.json", config);

    EXPECT_THROW(parse(testDir / "pcie_missing_device_path.json"),
                 std::runtime_error);
}

TEST_F(ConfigParserTest, ParsePCIeConfigMissingOffset)
{
    const std::string config = R"({
        "sibling_bmc_reset_gpio": {
            "name": "sibling-bmc-reset-n",
            "polarity": "low"
        },
        "bmc_configs": [],
        "pcie_config": {
            "device_path": "/dev/bmc-device0"
        }
    })";

    writeConfigFile("pcie_missing_offset.json", config);

    EXPECT_THROW(parse(testDir / "pcie_missing_offset.json"),
                 std::runtime_error);
}
