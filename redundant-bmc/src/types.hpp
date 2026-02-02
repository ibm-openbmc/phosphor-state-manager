// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <string>
#include <variant>

namespace rbmc
{

using FailoverOptions = std::map<std::string, std::variant<bool>>;

} // namespace rbmc
