// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <string>
#include <variant>

namespace rbmc
{

using FailoverOptions = std::map<std::string, std::variant<bool>>;

/**
 * @brief Events for tracking progress
 */
enum class Event
{
    failoverComplete,
    activeHandlerStartComplete,
    passiveHandlerStartComplete
};

} // namespace rbmc
