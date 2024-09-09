// SPDX-License-Identifier: Apache-2.0
#include "role_determination.hpp"

#include <gtest/gtest.h>

using namespace rbmc;
using namespace role_determination;

TEST(RoleDeterminationTest, RoleDeterminationTest)
{
    {
        Input input{.bmcPosition = 0};
        EXPECT_EQ(run(input), Role::Active);
    }

    {
        Input input{.bmcPosition = 1};
        EXPECT_EQ(run(input), Role::Passive);
    }
}
