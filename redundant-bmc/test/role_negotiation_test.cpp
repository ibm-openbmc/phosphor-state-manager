// SPDX-License-Identifier: Apache-2.0
#include "role_determination.hpp"

#include <ranges>

#include <gtest/gtest.h>

using namespace rbmc;
using namespace role_determination;

// Simulates what Manager::startup does when when determining roles
// so when both BMCs reach startup at exactly the same time that all
// possible combinations of previous roles (active/passive/unknown)
// lead to the correct chosen roles.
//
// It runs determineRole for each BMC, then needDeferToSibling for each BMC.
// if needDeferToSibling returns true, calls determineRole again with the
// sibling role its determineRole returned, which simulates letting
// the sibling BMC determine its role and then using that value.

using enum Role;

// Simulate the two-BMC startup negotiation and return {bmc0Role, bmc1Role}.
std::pair<Role, Role> negotiate(Role bmc0Prev, Role bmc1Prev)
{
    const size_t bmc0Pos = 0;
    const size_t bmc1Pos = 1;

    // BMC 1: determineRole() with sibling role unknown
    Input bmc1Input{.bmcPosition = bmc1Pos,
                    .previousRole = bmc1Prev,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};
    auto bmc1RoleInfo = determineRole(bmc1Input);

    // BMC 0: determineRole() with sibling role unknown
    Input bmc0Input{.bmcPosition = bmc0Pos,
                    .previousRole = bmc0Prev,
                    .siblingRole = Unknown,
                    .siblingAlive = true,
                    .failoverInProgress = false,
                    .siblingFailoverInProgress = false};
    auto bmc0RoleInfo = determineRole(bmc0Input);

    // BMC 1 calls needDeferToSibling. Should always return false.
    EXPECT_FALSE(needDeferToSibling(bmc1RoleInfo, bmc1Pos, std::nullopt));

    // BMC 0 calls need to defer sibling.  if true, call determineRole again
    if (needDeferToSibling(bmc0RoleInfo, bmc0Pos, std::nullopt))
    {
        // BMC 0 waited for BMC 1 to publish. Try again with sibling's new role
        bmc0Input.siblingRole = bmc1RoleInfo.role;
        bmc0RoleInfo = determineRole(bmc0Input);
    }

    return {bmc0RoleInfo.role, bmc1RoleInfo.role};
}

TEST(RoleNegotiationTest, NegotiatesCorrectRoles)
{
    struct Test
    {
        Role bmc0Prev;
        Role bmc1Prev;
        Role expectedBmc0;
        Role expectedBmc1;
    };

    // Run negotiate() for all possible previous role combinations.
    const std::array tests{
        // Both BMC's active -> BMC 1 active
        Test{Active, Active, Passive, Active},

        // Active/Passive -> Stays the same
        Test{Active, Passive, Active, Passive},

        // Active/Unknown -> BMC 0 stays active
        Test{Active, Unknown, Active, Passive},

        // Passive/Active -> stays the same
        Test{Passive, Active, Passive, Active},

        // Both passive -> BMC 0 now active
        Test{Passive, Passive, Active, Passive},

        // Passive/Unknown -> BMC 0 now active
        Test{Passive, Unknown, Active, Passive},

        // Unknown/Active -> BMC 1 stays active
        Test{Unknown, Active, Passive, Active},

        // Unknown/Passive -> BMC 0 now active
        Test{Unknown, Passive, Active, Passive},

        // Unknown/Unknown -> BMC 0 now active
        Test{Unknown, Unknown, Active, Passive},
    };

    for (const auto& [i, t] : std::views::enumerate(tests))
    {
        auto [bmc0Role, bmc1Role] = negotiate(t.bmc0Prev, t.bmc1Prev);
        EXPECT_EQ(bmc0Role, t.expectedBmc0)
            << "entry " << i << " — BMC 0 role check failed";
        EXPECT_EQ(bmc1Role, t.expectedBmc1)
            << "entry " << i << " — BMC 1 role check failed";
    }
}
