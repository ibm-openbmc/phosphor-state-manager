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
        input.codeVersionsMatch = false;
        input.siblingState = rbmc::BMCState::Quiesced;
        input.siblingRole = rbmc::Role::Unknown;

        auto reasons = getNoRedundancyReasons(input);

        EXPECT_EQ(reasons.size(), 3);
        EXPECT_TRUE(std::ranges::contains(reasons, codeMismatch));
        EXPECT_TRUE(std::ranges::contains(reasons, siblingNotAtReady));
        EXPECT_TRUE(std::ranges::contains(reasons, siblingNotPassive));
    }
}

TEST(RedundancyTest, GetNoRedundancyDescTest)
{
    EXPECT_EQ(getNoRedundancyDescription(NoRedundancyReason::codeMismatch),
              "Firmware version mismatch");
}

TEST(RedundancyTest, FailoversNotAllowedTest)
{
    namespace fona = rbmc::fona;
    using enum fona::FailoversNotAllowedReason;

    std::map<rbmc::SystemState, fona::FailoversNotAllowedReasons> testStates{
        {rbmc::SystemState::off, {}},
        {rbmc::SystemState::booting, {systemState}},
        {rbmc::SystemState::runtime, {}},
        {rbmc::SystemState::other, {systemState}}};

    for (const auto& [state, expectedReasons] : testStates)
    {
        fona::Input input{.redundancyEnabled = true,
                          .fullSyncComplete = true,
                          .systemState = state};

        auto reasons = fona::getFailoversNotAllowedReasons(input);
        EXPECT_EQ(reasons, expectedReasons);
    }

    // Redundancy disabled
    {
        fona::Input input{.redundancyEnabled = false,
                          .fullSyncComplete = true,
                          .systemState = rbmc::SystemState::off};
        auto reasons = fona::getFailoversNotAllowedReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(),
                  fona::FailoversNotAllowedReason::redundancyDisabled);
    }

    // Full sync not complete
    {
        fona::Input input{.redundancyEnabled = true,
                          .fullSyncComplete = false,
                          .systemState = rbmc::SystemState::off};
        auto reasons = fona::getFailoversNotAllowedReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(),
                  fona::FailoversNotAllowedReason::fullSyncNotComplete);
    }
}

TEST(RedundancyTest, GetFailoversNotAllowedDescTest)
{
    namespace fona = rbmc::fona;

    EXPECT_EQ(fona::getFailoversNotAllowedDescription(
                  fona::FailoversNotAllowedReason::systemState),
              "System state is not off or runtime");
}
