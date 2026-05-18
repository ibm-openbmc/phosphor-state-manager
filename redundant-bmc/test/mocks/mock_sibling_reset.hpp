// SPDX-License-Identifier: Apache-2.0
#pragma once

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
};

} // namespace rbmc
