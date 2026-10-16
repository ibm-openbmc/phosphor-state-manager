// SPDX-License-Identifier: Apache-2.0
#include "role_determination.hpp"

#include <ranges>

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

    // failoverInProgress takes priority even with no previous role.
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = true,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Active, failoverInProgress};
        EXPECT_EQ(determineRole(input), info);
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

    // siblingFailoverInProgress also overrides a passive previous role
    // on pos 1.
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Passive,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = true};

        RoleInfo info{Passive, siblingFailoverInProgress};
        EXPECT_EQ(determineRole(input), info);
    }

    // siblingNotAlive on pos 0: sibling-not-alive takes priority
    // over position.
    {
        Input input{.bmcPosition = 0,
                    .previousRole = Unknown,
                    .siblingRole = Unknown,
                    .siblingAlive = false,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Active, siblingNotAlive};
        EXPECT_EQ(determineRole(input), info);
    }

    // Sibling already active on pos 1.  Just like above.
    {
        Input input{.bmcPosition = 1,
                    .previousRole = Unknown,
                    .siblingRole = Active,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};

        RoleInfo info{Passive, siblingActive};
        EXPECT_EQ(determineRole(input), info);
    }
}

TEST(RoleDeterminationTest, ErrorReasonTest)
{
    EXPECT_TRUE(isErrorReason(RoleReason::notPaired));
    EXPECT_FALSE(isErrorReason(RoleReason::resumePrevious));
}

TEST(RoleDeterminationTest, NeedDeferToSiblingTest)
{
    using enum Role;
    using enum RoleReason;

    struct Test
    {
        RoleInfo info;
        size_t pos;
        std::optional<Role> siblingRole;
        bool needDefer;
    };

    const std::array tests{
        // BMC 0 going active only because of its position, sibling role
        // unknown: wait to preserve sibling's previous active role.
        Test{{Active, positionZero}, 0, std::nullopt, true},

        // Same but sibling role passed as Role::Unknown explicitly — same
        // result as nullopt since value_or(Unknown) == Unknown.
        Test{{Active, positionZero}, 0, Unknown, true},

        // BMC 0 going active via resumePrevious, sibling Unknown: defer to
        // let BMC 1 publish its role first.
        Test{{Active, resumePrevious}, 0, std::nullopt, true},

        // BMC 0 going passive via resumePrevious, sibling Unknown: defer to
        // let BMC 1 publish its role first.
        Test{{Passive, resumePrevious}, 0, std::nullopt, true},

        Test{{Active, failover}, 0, std::nullopt, false},

        Test{{Active, positionZero}, 0, Passive, false},

        Test{{Active, positionZero}, 0, Active, false},

        Test{{Passive, positionZero}, 0, std::nullopt, false},

        Test{{Active, resumePrevious}, 0, Active, false},

        Test{{Active, resumePrevious}, 0, Passive, false},

        Test{{Active, resumePrevious}, 1, std::nullopt, false},

        Test{{Passive, resumePrevious}, 1, std::nullopt, false},
    };

    for (const auto& [i, t] : std::views::enumerate(tests))
    {
        EXPECT_EQ(needDeferToSibling(t.info, t.pos, t.siblingRole), t.needDefer)
            << "Test entry " << i << " failed";
    }
}
