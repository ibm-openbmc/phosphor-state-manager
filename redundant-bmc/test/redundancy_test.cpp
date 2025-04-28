// SPDX-License-Identifier: Apache-2.0
#include "redundancy.hpp"

#include <gtest/gtest.h>

using namespace rbmc::redundancy;

TEST(RedundancyTest, NoRedundancyReasonsTest)
{
    using enum NoRedundancyReason;

    // Golden inputs - redundancy can be enabled.
    const Input golden{
        .role = rbmc::Role::Active,
        .siblingPresent = true,
        .siblingHeartbeat = true,
        .siblingProvisioned = true,
        .siblingHasSiblingComm = true,
        .siblingRole = rbmc::Role::Passive,
        .siblingState = rbmc::BMCState::Ready,
        .codeVersionsMatch = true,
        .manualDisable = false,
        .redundancyOffAtRuntimeStart = false,
        .syncFailed = false};

    // Nothing stopping redundancy
    {
        auto reasons = getNoRedundancyReasons(golden);
        EXPECT_TRUE(reasons.empty());
    }

    // Not active
    {
        auto input = golden;
        input.role = rbmc::Role::Unknown;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), bmcNotActive);
    }

    // No sibling
    {
        auto input = golden;
        input.siblingPresent = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), siblingMissing);
    }

    // No sibling heartbeat
    {
        auto input = golden;
        input.siblingHeartbeat = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), noSiblingHeartbeat);
    }

    // Sibling isn't provisioned
    {
        auto input = golden;
        input.siblingProvisioned = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), siblingNotProvisioned);
    }

    // Sibling isn't passive
    {
        auto input = golden;
        input.siblingRole = rbmc::Role::Unknown;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), siblingNotPassive);
    }

    // Sibling can't talk to this BMC
    {
        auto input = golden;
        input.siblingHasSiblingComm = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), siblingNoCommunication);
    }

    // FW versions don't match
    {
        auto input = golden;
        input.codeVersionsMatch = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), codeMismatch);
    }

    // Sibling is in Quiesce state
    {
        auto input = golden;
        input.siblingState = rbmc::BMCState::Quiesced;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), siblingNotAtReady);
    }

    // Redundancy is manually disabled
    {
        auto input = golden;
        input.manualDisable = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), manuallyDisabled);
    }

    // Redundancy was off at runtime
    {
        auto input = golden;
        input.redundancyOffAtRuntimeStart = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), redundancyOffAtRuntimeStart);
    }

    // Sync failed
    {
        auto input = golden;
        input.syncFailed = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), syncFailed);
    }

    // Multiple fails
    {
        auto input = golden;
        input.codeVersionsMatch = false;               // codeMismatch
        input.siblingState = rbmc::BMCState::Quiesced; // siblingHealth
        input.siblingHasSiblingComm = false;           // systemHealth
        input.siblingRole = rbmc::Role::Unknown;       // siblingHealth

        auto reasons = getNoRedundancyReasons(input);

        EXPECT_EQ(reasons.size(), 4);
        EXPECT_TRUE(std::ranges::contains(reasons, codeMismatch));
        EXPECT_TRUE(std::ranges::contains(reasons, siblingNotAtReady));
        EXPECT_TRUE(std::ranges::contains(reasons, siblingNoCommunication));
        EXPECT_TRUE(std::ranges::contains(reasons, siblingNotPassive));
    }
}

TEST(RedundancyTest, GetNoRedundancyDescTest)
{
    EXPECT_EQ(getNoRedundancyDescription(NoRedundancyReason::codeMismatch),
              "Firmware version mismatch");
}

TEST(RedundancyTest, FailoversPausedTest)
{
    namespace fop = rbmc::fop;
    using enum fop::FailoversPausedReason;

    std::map<rbmc::SystemState, fop::FailoversPausedReasons> testStates{
        {rbmc::SystemState::off, {}},
        {rbmc::SystemState::booting, {systemState}},
        {rbmc::SystemState::runtime, {}},
        {rbmc::SystemState::other, {systemState}}};

    for (const auto& [state, expectedReasons] : testStates)
    {
        fop::Input input{.systemState = state};

        auto reasons = fop::getFailoversPausedReasons(input);
        EXPECT_EQ(reasons, expectedReasons);
    }
}

TEST(RedundancyTest, GetFailoversPausedDescTest)
{
    namespace fop = rbmc::fop;

    EXPECT_EQ(fop::getFailoversPausedDescription(
                  fop::FailoversPausedReason::systemState),
              "System state is not off or runtime");
}
