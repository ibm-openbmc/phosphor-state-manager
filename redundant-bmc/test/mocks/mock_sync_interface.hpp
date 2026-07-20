// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "async_helpers.hpp"
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

    /**
     * @brief Setup default behaviors for common test scenarios to save
     *        setup in the testcases.
     */
    void setupDefaultBehavior()
    {
        ON_CALL(*this, doFullSync()).WillByDefault([this]() {
            SyncInterface::fullSyncComplete = true;
            return test_helpers::makeCompletedTask(true);
        });

        ON_CALL(*this, disableBackgroundSync()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });
    }
};

} // namespace rbmc
