// SPDX-License-Identifier: Apache-2.0
#include "role_determination.hpp"

#include <gtest/gtest.h>

using namespace rbmc;
using namespace role_determination;

TEST(RoleDeterminationTest, RoleDeterminationTest)
{
    using enum Role;
    using enum RoleReason;

    // BMC pos 0 with sibling healthy
    {
        Input input{.bmcPosition = 0,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Active, positionZero};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason), "BMC is position 0");
    }

    // BMC pos 1 with sibling healthy
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};
        RoleInfo info{Passive, positionNonzero};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "BMC is not position 0");
    }

    // No Sibling heartbeat, BMC pos 1
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingAlive = false,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Active, siblingNotAlive};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason), "Sibling not alive");
    }

    // Sibling already active, this pos = 0
    {
        Input input{.bmcPosition = 0,
                    .previousRole = Unknown,
                    .siblingRole = Active,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Passive, siblingActive};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Sibling is already active");
    }

    // Sibling already passive, this pos = 1
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Passive,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Active, siblingPassive};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Sibling is already passive");
    }

    // BMC pos 0 with sibling healthy, previous role = Passive
    {
        Input input{.bmcPosition = 0,
                    .previousRole = Passive,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        // Preserve passive
        RoleInfo info{Passive, resumePrevious};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Resuming previous role");
    }

    // BMC pos 1 with sibling healthy, previous role = Active
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Active,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        // Preserve active
        RoleInfo info{Active, resumePrevious};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Resuming previous role");
    }

    // Simulate a reboot in the middle of a failover on
    // the active BMC - failover in progress = true.
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Active,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = true,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Active, failoverInProgress};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Newly active from failover");
    }

    // Simulate a passive BMC coming back from a failover
    // when the new active BMC was rebooted and just came back.
    // With siblingFailoverInProgress set it won't resume
    // previous role of active.
    {
        Input input{.bmcPosition = 0,
                    .previousRole = Active,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = true};

        RoleInfo info{Passive, siblingFailoverInProgress};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Sibling was driving a failover");
    }
}

TEST(RoleDeterminationTest, ErrorReasonTest)
{
    EXPECT_TRUE(isErrorReason(RoleReason::notProvisioned));
    EXPECT_FALSE(isErrorReason(RoleReason::resumePrevious));
}
