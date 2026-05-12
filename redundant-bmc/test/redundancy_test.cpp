// SPDX-License-Identifier: Apache-2.0
#include "redundancy.hpp"

#include <gtest/gtest.h>

using namespace rbmc::redundancy;

TEST(RedundancyTest, NoRedundancyReasonsTest)
{
    using enum rbmc::ReasonForNoRedundancy;

    // Golden inputs - redundancy can be enabled.
    const Input golden{
        .role = rbmc::Role::Active,
        .siblingPresent = true,
        .siblingAlive = true,
        .siblingPaired = true,
        .siblingRole = rbmc::Role::Passive,
        .siblingCannotBeActive = false,
        .siblingState = rbmc::BMCState::Ready,
        .codeVersionsMatch = true,
        .manualDisable = false,
        .redundancyOffAtRuntimeStart = false,
        .syncFailed = false,
        .peerConnected = true,
        .passiveHWIssue = false};

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
        EXPECT_EQ(*reasons.begin(), BMCNotActive);
    }

    // No sibling
    {
        auto input = golden;
        input.siblingPresent = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SiblingMissing);
    }

    // Sibling not alive
    {
        auto input = golden;
        input.siblingAlive = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SiblingNotAlive);
    }

    // Sibling isn't paired
    {
        auto input = golden;
        input.siblingPaired = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SiblingNotProvisioned);
    }

    // Sibling isn't passive
    {
        auto input = golden;
        input.siblingRole = rbmc::Role::Unknown;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SiblingNotPassive);
    }

    // FW versions don't match
    {
        auto input = golden;
        input.codeVersionsMatch = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), CodeVersionMismatch);
    }

    // Sibling is in Quiesce state
    {
        auto input = golden;
        input.siblingState = rbmc::BMCState::Quiesced;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SiblingNotAtReady);
    }

    // Sibling cannot be active
    {
        auto input = golden;
        input.siblingCannotBeActive = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SiblingCannotBeActive);
    }

    // Redundancy is manually disabled
    {
        auto input = golden;
        input.manualDisable = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), ManuallyDisabled);
    }

    // Redundancy was off at runtime
    {
        auto input = golden;
        input.redundancyOffAtRuntimeStart = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), RedundancyOffAtRuntimeStart);
    }

    // Network failure
    {
        auto input = golden;
        input.peerConnected = false;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), NetworkError);
    }

    // Sync failed
    {
        auto input = golden;
        input.syncFailed = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), DataSyncFailed);
    }

    // Passive hardware issue
    {
        auto input = golden;
        input.passiveHWIssue = true;

        auto reasons = getNoRedundancyReasons(input);
        ASSERT_EQ(reasons.size(), 1);
        EXPECT_EQ(*reasons.begin(), SystemHWConfigIssue);
    }

    // Multiple fails
    {
        auto input = golden;
        input.codeVersionsMatch = false;
        input.siblingState = rbmc::BMCState::Quiesced;
        input.siblingRole = rbmc::Role::Unknown;

        auto reasons = getNoRedundancyReasons(input);

        EXPECT_EQ(reasons.size(), 3);
        EXPECT_TRUE(std::ranges::contains(reasons, CodeVersionMismatch));
        EXPECT_TRUE(std::ranges::contains(reasons, SiblingNotAtReady));
        EXPECT_TRUE(std::ranges::contains(reasons, SiblingNotPassive));
    }
}

TEST(RedundancyTest, FailoversNotAllowedTest)
{
    namespace fona = rbmc::fona;
    using enum rbmc::FailoversNotAllowedReason;

    std::map<rbmc::SystemState, rbmc::FailoversNotAllowedReason> testStates{
        {rbmc::SystemState::off, None},
        {rbmc::SystemState::booting, WrongSystemState},
        {rbmc::SystemState::runtime, None},
        {rbmc::SystemState::other, WrongSystemState}};

    for (const auto& [state, expectedReason] : testStates)
    {
        fona::Input input{.redundancyEnabled = true,
                          .fullSyncComplete = true,
                          .failoverInProgress = false,
                          .systemState = state};

        auto reason = fona::getFailoversNotAllowedReason(input);
        EXPECT_EQ(reason, expectedReason);
    }

    // Redundancy disabled
    {
        fona::Input input{.redundancyEnabled = false,
                          .fullSyncComplete = true,
                          .failoverInProgress = false,
                          .systemState = rbmc::SystemState::off};
        auto reason = fona::getFailoversNotAllowedReason(input);
        EXPECT_EQ(reason, NoRedundancy);
    }

    // Full sync not complete
    {
        fona::Input input{.redundancyEnabled = true,
                          .fullSyncComplete = false,
                          .failoverInProgress = false,
                          .systemState = rbmc::SystemState::off};
        auto reason = fona::getFailoversNotAllowedReason(input);
        EXPECT_EQ(reason, FullSyncNotComplete);
    }

    // Failover in progress
    {
        fona::Input input{.redundancyEnabled = true,
                          .fullSyncComplete = true,
                          .failoverInProgress = true,
                          .systemState = rbmc::SystemState::off};
        auto reason = fona::getFailoversNotAllowedReason(input);
        EXPECT_EQ(reason, FailoverInProgress);
    }
}

TEST(RedundancyTest, GetFailoversNotAllowedDescTest)
{
    namespace fona = rbmc::fona;
    using enum rbmc::FailoversNotAllowedReason;

    EXPECT_EQ(fona::getFailoversNotAllowedDescription(WrongSystemState),
              "System state is not off or runtime");
}

TEST(RedundancyTest, FailoverBlockedTest)
{
    rbmc::fo_blocked::Input golden{
        .siblingAlive = true,
        .siblingState = rbmc::BMCState::Ready,
        .redundancyEnabled = true,
        .state = rbmc::BMCState::Ready,
        .failoversNotAllowed = false,
        .forceOption = false,
        .failoverInProgress = false,
        .lastKnownRedundancyEnabled = true};

    EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(golden),
              rbmc::fo_blocked::Reason::none);

    // Redundancy not enabled
    {
        auto input = golden;
        input.redundancyEnabled = false;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::redundancyNotEnabled);
    }

    // Failovers not allowed
    {
        auto input = golden;
        input.failoversNotAllowed = true;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::failoversNotAllowed);
    }

    // Failovers not allowed, but forced
    {
        auto input = golden;
        input.failoversNotAllowed = true;
        input.forceOption = true;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::none);
    }

    // Failovers not allowed, but sibling is Quiesced
    {
        auto input = golden;
        input.failoversNotAllowed = true;
        input.siblingState = rbmc::BMCState::Quiesced;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::none);
    }

    // Sibling not responding, but redundancy was enabled
    {
        auto input = golden;
        input.siblingAlive = false;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::none);
    }

    // Sibling not responding, redundancy was enabled and failovers not allowed
    {
        auto input = golden;
        input.siblingAlive = false;
        input.failoversNotAllowed = true;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::none);
    }

    // Sibling not responding, redundancy was enabled, but this BMC in Quiesced.
    {
        auto input = golden;
        input.siblingAlive = false;
        input.state = rbmc::BMCState::Quiesced;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::notAtReady);
    }

    // Sibling not responding, but redundancy wasn't enabled
    {
        auto input = golden;
        input.siblingAlive = false;
        input.lastKnownRedundancyEnabled = false;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::siblingDeadButRedundancyNotEnabled);
    }

    // Failover in Progress
    {
        auto input = golden;
        input.failoverInProgress = true;
        EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedReason(input),
                  rbmc::fo_blocked::Reason::failoverAlreadyInProgress);
    }
}

TEST(RedundancyTest, GetNoFailoverDescTest)
{
    EXPECT_EQ(rbmc::fo_blocked::getFailoverBlockedDescription(
                  rbmc::fo_blocked::Reason::redundancyNotEnabled),
              "Redundancy is not enabled");
}
