// SPDX-License-Identifier: Apache-2.0

#include "role_determination.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc::role_determination
{

Role run(const Input& input)
{
    if (input.bmcPosition == 0)
    {
        lg2::info("Role = active due to BMC position 0");
        return Role::Active;
    }
    return Role::Passive;
}

} // namespace rbmc::role_determination
