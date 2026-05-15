// SPDX-License-Identifier: Apache-2.0
#include "persistent_data_test_fixture.hpp"
#include "types.hpp"
#include "util.hpp"

#include <fstream>

#include <gtest/gtest.h>

using namespace rbmc::util;
using namespace rbmc::test;
using Failover = sdbusplus::common::xyz::openbmc_project::control::Failover;

class UtilTest : public PersistentDataTestFixture
{
  protected:
    void SetUp() override
    {
        char dirTemplate[] = "/tmp/utils_testXXXXXX";
        char* dir = mkdtemp(dirTemplate);
        if (dir == nullptr)
        {
            throw std::runtime_error("Failed to create temp directory");
        }
        testDir = dir;
    }

    void TearDown() override
    {
        std::filesystem::remove_all(testDir);
    }

    void createOSReleaseFile(const std::string& content)
    {
        std::ofstream file(testDir / "os-release");
        file << content;
        file.close();
    }

    std::filesystem::path testDir;
};

TEST_F(UtilTest, ExternalRedundancyInputTest)
{
    using RedundancyInput = sdbusplus::common::xyz::openbmc_project::state::
        bmc::Redundancy::RedundancyInput;

    // Initially, no inputs should be set
    auto inputs = readExternalRedundancyInputs();
    EXPECT_TRUE(inputs.empty());

    // Not set yet
    EXPECT_FALSE(
        hasExternalRedundancyInput(RedundancyInput::PassiveBMCHardwareProblem));

    EXPECT_FALSE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem,
        RedundancyInput::PassiveBMCHardwareProblem));

    // Add an input
    writeExternalRedundancyInput(RedundancyInput::PassiveBMCHardwareProblem,
                                 true);

    // Verify it was added
    EXPECT_TRUE(
        hasExternalRedundancyInput(RedundancyInput::PassiveBMCHardwareProblem));

    // Other one still not yet
    EXPECT_FALSE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem));

    EXPECT_TRUE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem,
        RedundancyInput::PassiveBMCHardwareProblem));

    // Read all inputs and verify
    inputs = readExternalRedundancyInputs();
    ASSERT_EQ(inputs.size(), 1);
    EXPECT_TRUE(inputs.contains(RedundancyInput::PassiveBMCHardwareProblem));

    // Add another
    writeExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem, true);
    inputs = readExternalRedundancyInputs();
    EXPECT_EQ(inputs.size(), 2);

    // Verify it was added
    EXPECT_TRUE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem));

    // Clear the first one only
    writeExternalRedundancyInput(RedundancyInput::PassiveBMCHardwareProblem,
                                 false);

    EXPECT_FALSE(
        hasExternalRedundancyInput(RedundancyInput::PassiveBMCHardwareProblem));
    EXPECT_TRUE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem));

    // Now clear the other one
    writeExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem, false);
    EXPECT_FALSE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem));
    EXPECT_FALSE(hasExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem,
        RedundancyInput::PassiveBMCHardwareProblem));

    inputs = readExternalRedundancyInputs();
    EXPECT_EQ(inputs.size(), 0);

    // Add them back
    writeExternalRedundancyInput(RedundancyInput::PassiveBMCHardwareProblem,
                                 true);
    writeExternalRedundancyInput(
        RedundancyInput::PassiveBMCHostProcessorProblem, true);
    // Clear all inputs
    bool cleared = clearExternalRedundancyInputs();
    EXPECT_TRUE(cleared);

    // Verify inputs are cleared
    inputs = readExternalRedundancyInputs();
    EXPECT_TRUE(inputs.empty());

    // Clearing when already empty should return false
    cleared = clearExternalRedundancyInputs();
    EXPECT_FALSE(cleared);
}

TEST_F(UtilTest, GetFailoverOption_EmptyOptions)
{
    // Test with empty FailoverOptions
    rbmc::FailoverOptions options;

    auto result = getFailoverOption<bool>(Failover::Options::Force, options);

    EXPECT_FALSE(result.has_value());
}

TEST_F(UtilTest, GetFailoverOption_OptionNotPresent)
{
    // Test when the requested option is not in the map
    rbmc::FailoverOptions options{{"SomeOtherOption", true}};

    auto result = getFailoverOption<bool>(Failover::Options::Force, options);

    EXPECT_FALSE(result.has_value());
}

TEST_F(UtilTest, GetFailoverOption_MultipleOptions)
{
    rbmc::FailoverOptions options{
        {Failover::convertOptionsToString(Failover::Options::Force), true},
        {Failover::convertOptionsToString(
             Failover::Options::UseRedundancyInput),
         "PassiveBMCHardwareProblem"}};

    auto forceResult =
        getFailoverOption<bool>(Failover::Options::Force, options);

    auto hwProblemResult = getFailoverOption<std::string>(
        Failover::Options::UseRedundancyInput, options);

    ASSERT_TRUE(forceResult.has_value());
    EXPECT_TRUE(forceResult.value());

    ASSERT_TRUE(hwProblemResult.has_value());
    EXPECT_EQ(hwProblemResult.value(), "PassiveBMCHardwareProblem");
}

TEST_F(UtilTest, ValidateFailoverRedundancyInput_OptionNotPassed)
{
    rbmc::FailoverOptions options;

    bool result = validateFailoverRedundancyInput(options);

    EXPECT_TRUE(result);
}

TEST_F(UtilTest, ValidateFailoverRedundancyInput_ValidValue)
{
    using RedundancyInterface =
        sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

    rbmc::FailoverOptions options{
        {Failover::convertOptionsToString(
             Failover::Options::UseRedundancyInput),
         RedundancyInterface::convertRedundancyInputToString(
             RedundancyInterface::RedundancyInput::PassiveBMCHardwareProblem)}};

    bool result = validateFailoverRedundancyInput(options);

    EXPECT_TRUE(result);
}

TEST_F(UtilTest, ValidateFailoverRedundancyInput_InvalidValue)
{
    rbmc::FailoverOptions options{{Failover::convertOptionsToString(
                                       Failover::Options::UseRedundancyInput),
                                   "InvalidRedundancyInputValue"}};

    bool result = validateFailoverRedundancyInput(options);

    EXPECT_FALSE(result);
}

TEST_F(UtilTest, GetOSReleaseValueWithQuotes)
{
    constexpr auto content = R"(NAME="Test OS"
VERSION_ID="1.2.3"
ID=test
)";
    createOSReleaseFile(content);

    auto result = getOSReleaseValue(testDir / "os-release", "VERSION_ID");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "1.2.3");
}

TEST_F(UtilTest, GetOSReleaseValueWithoutQuotes)
{
    constexpr auto content = R"(VERSION_ID=1.2.3
)";
    createOSReleaseFile(content);

    auto result = getOSReleaseValue(testDir / "os-release", "VERSION_ID");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "1.2.3");
}

TEST_F(UtilTest, GetOSReleaseValueNotFound)
{
    constexpr auto content = R"(NAME="Test OS"
ID=test
)";
    createOSReleaseFile(content);

    auto result = getOSReleaseValue(testDir / "os-release", "VERSION_ID");

    EXPECT_FALSE(result.has_value());
}

TEST_F(UtilTest, GetOSReleaseValueFileNotFound)
{
    auto result = getOSReleaseValue(testDir / "nonexistent.txt", "VERSION_ID");

    EXPECT_FALSE(result.has_value());
}

TEST_F(UtilTest, GetOSReleaseValueEmptyValue)
{
    constexpr auto content = R"(VERSION_ID=""
)";
    createOSReleaseFile(content);

    auto result = getOSReleaseValue(testDir / "os-release", "VERSION_ID");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "");
}

TEST_F(UtilTest, GetOSReleaseValueEmptyValueNoQuotes)
{
    constexpr auto content = R"(VERSION_ID=
)";
    createOSReleaseFile(content);

    auto result = getOSReleaseValue(testDir / "os-release", "VERSION_ID");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "");
}
