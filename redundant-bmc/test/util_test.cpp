#include "persistent_data_test_fixture.hpp"
#include "util.hpp"

#include <gtest/gtest.h>

using namespace rbmc::util;
using namespace rbmc::test;

class UtilTest : public PersistentDataTestFixture
{};

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
