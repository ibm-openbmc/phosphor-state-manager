// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>

namespace test_helpers
{

/**
 * @brief Helper to create a completed coroutine task with no return value
 *
 * Use this for mocking methods that return sdbusplus::async::task<>
 */
inline sdbusplus::async::task<> makeCompletedTask()
{
    co_return;
}

/**
 * @brief Generic helper to create a completed coroutine task for any type
 *
 * @tparam T - The return type
 * @param value - The value to return
 * @return Completed task with the specified value
 */
template <typename T>
inline sdbusplus::async::task<T> makeCompletedTask(T value)
{
    co_return value;
}

} // namespace test_helpers
