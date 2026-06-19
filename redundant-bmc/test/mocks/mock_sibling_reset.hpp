// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "async_helpers.hpp"
#include "sibling_reset.hpp"

#include <gmock/gmock.h>

namespace rbmc
{

/**
 * @class MockSiblingReset
 *
 * Mock implementation of the SiblingReset interface for testing.
 */
class MockSiblingReset : public testing::NiceMock<SiblingReset>
{
  public:
    MockSiblingReset() = default;
    ~MockSiblingReset() override = default;

    MOCK_METHOD(void, assertReset, (), (override));
    MOCK_METHOD(void, releaseReset, (), (override));
    MOCK_METHOD(sdbusplus::async::task<>, toggleReset, (), (override));

    /**
     * @brief Setup default behaviors for common test scenarios to save
     *        setup in the testcases.
     */
    void setupDefaultBehavior()
    {
        ON_CALL(*this, toggleReset()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });
    }
};

} // namespace rbmc
