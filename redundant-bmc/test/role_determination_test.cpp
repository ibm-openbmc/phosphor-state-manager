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
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        RoleInfo info{Active, positionZero};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason), "BMC is position 0");
    }

    // BMC pos 1 with sibling healthy
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};
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
                    .siblingHeartbeat = false,
                    .siblingProvisioned = true};

        RoleInfo info{Active, noSiblingHeartbeat};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "No sibling heartbeat");
    }

    // Sibling not provisioned
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = false};

        RoleInfo info{Active, siblingNotProvisioned};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Sibling is not provisioned");
    }

    // Sibling already active, this pos = 0
    {
        Input input{.bmcPosition = 0,
                    .previousRole = Unknown,
                    .siblingRole = Active,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

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
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

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
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

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
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        // Preserve active
        RoleInfo info{Active, resumePrevious};
        EXPECT_EQ(determineRole(input), info);
        EXPECT_EQ(getRoleReasonDescription(info.reason),
                  "Resuming previous role");
    }
}

TEST(RoleDeterminationTest, ErrorReasonTest)
{
    EXPECT_TRUE(isErrorReason(RoleReason::notProvisioned));
    EXPECT_FALSE(isErrorReason(RoleReason::resumePrevious));
}
