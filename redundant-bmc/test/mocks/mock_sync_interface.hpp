// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sync_interface.hpp"

#include <gmock/gmock.h>

namespace rbmc
{

/**
 * @class MockSyncInterface
 *
 * Mock implementation of the SyncInterface for testing.
 */
class MockSyncInterface : public testing::NiceMock<SyncInterface>
{
  public:
    MockSyncInterface() = default;
    ~MockSyncInterface() override = default;

    MOCK_METHOD(sdbusplus::async::task<bool>, doFullSync, (), (override));
    MOCK_METHOD(sdbusplus::async::task<>, disableBackgroundSync, (),
                (override));
};

} // namespace rbmc
