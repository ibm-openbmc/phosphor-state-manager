// SPDX-License-Identifier: Apache-2.0
#include "role_determination.hpp"

#include <gtest/gtest.h>

using namespace rbmc;
using namespace role_determination;

TEST(RoleDeterminationTest, RoleDeterminationTest)
{
    using enum ErrorCase;
    using enum Role;

    // BMC pos 0 with sibling healthy
    {
        Input input{.bmcPosition = 0,
                    .siblingPosition = 1,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        RoleInfo info{Active, noError};
        EXPECT_EQ(run(input), info);
    }

    // BMC pos 1 with sibling healthy
    {
        Input input{.bmcPosition = 1,
                    .siblingPosition = 0,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        RoleInfo info{Passive, noError};
        EXPECT_EQ(run(input), info);
    }

    // No Sibling heartbeat, BMC pos 1
    {
        Input input{.bmcPosition = 1,
                    .siblingPosition = 0,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = false,
                    .siblingProvisioned = true};

        RoleInfo info{Active, noError};
        EXPECT_EQ(run(input), info);
    }

    // Both BMCs report the same position
    {
        Input input{.bmcPosition = 0,
                    .siblingPosition = 0,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        RoleInfo info{Passive, samePositions};
        EXPECT_EQ(run(input), info);
    }

    // Sibling not provisioned
    {
        Input input{.bmcPosition = 1,
                    .siblingPosition = 0,
                    .siblingRole = Unknown,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = false};

        RoleInfo info{Active, noError};
        EXPECT_EQ(run(input), info);
    }

    // Sibling already active, this pos = 0
    {
        Input input{.bmcPosition = 0,
                    .siblingPosition = 1,
                    .siblingRole = Active,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        RoleInfo info{Passive, noError};
        EXPECT_EQ(run(input), info);
    }

    // Sibling already passive, this pos = 1
    {
        Input input{.bmcPosition = 1,
                    .siblingPosition = 0,
                    .siblingRole = Passive,
                    .siblingHeartbeat = true,
                    .siblingProvisioned = true};

        RoleInfo info{Active, noError};
        EXPECT_EQ(run(input), info);
    }
}
