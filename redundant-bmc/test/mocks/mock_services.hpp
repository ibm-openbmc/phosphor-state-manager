// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "services.hpp"

#include <gmock/gmock.h>

namespace rbmc
{

/**
 * @class MockServices
 *
 * Mock implementation of the Services interface for testing.
 */
class MockServices : public testing::NiceMock<Services>
{
  public:
    MockServices() = default;
    ~MockServices() override = default;

    MOCK_METHOD(sdbusplus::async::task<>, init, (), (override));

    MOCK_METHOD(bool, getPaired, (), (const, override));

    MOCK_METHOD(std::optional<size_t>, getBMCPosition, (), (const, override));

    MOCK_METHOD(sdbusplus::async::task<bool>, checkSystemInventoryStatus, (),
                (override));

    MOCK_METHOD(std::filesystem::path, getPersistentDataPath, (),
                (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, logError,
                (std::string error, errors::Level severity,
                 errors::AdditionalData data),
                (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, startUnit,
                (const std::string& unitName, std::chrono::seconds timeout),
                (const, override));

    MOCK_METHOD(sdbusplus::async::task<std::string>, getUnitState,
                (const std::string& unitName), (const, override));

    MOCK_METHOD(std::string, getFWVersion, (), (const, override));

    MOCK_METHOD(
        sdbusplus::async::task<
            sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState>,
        getBMCState, (), (const, override));

    MOCK_METHOD(SystemState, getSystemState, (), (const, override));

    MOCK_METHOD(bool, getPeerConnected, (), (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, waitForPeerConnection, (),
                (override));

    MOCK_METHOD(sdbusplus::async::task<>, doFailoverImminentDelay, (),
                (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, flushJournal, (), (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, acquireFullHardwareAccess, (),
                (override));

    MOCK_METHOD(void, setRedundancyDetermined, (), (override));

    MOCK_METHOD(sdbusplus::async::task<>, waitForSelfPairing, (), (override));
};

} // namespace rbmc
