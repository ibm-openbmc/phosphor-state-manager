// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "async_helpers.hpp"
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

    MOCK_METHOD(sdbusplus::async::task<>, waitForPeerConnection,
                (AbortPredicate), (override));

    MOCK_METHOD(sdbusplus::async::task<>, doFailoverImminentDelay, (),
                (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, flushJournal, (), (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, acquireFullHardwareAccess, (),
                (override));

    MOCK_METHOD(void, setRedundancyDetermined, (), (override));

    MOCK_METHOD(sdbusplus::async::task<>, waitForSelfPairing, (), (override));

    /**
     * @brief Setup default behaviors for common test scenarios to save
     *        setup in the testcases.
     */
    void setupDefaultBehavior()
    {
        using ::testing::_;
        using ::testing::Return;

        ON_CALL(*this, init()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, logError(_, _, _)).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, getSystemState())
            .WillByDefault(Return(SystemState::off));

        ON_CALL(*this, getPeerConnected()).WillByDefault(Return(true));

        ON_CALL(*this, waitForPeerConnection(_)).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, startUnit(_, _)).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, acquireFullHardwareAccess()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, getBMCState()).WillByDefault([]() {
            using BMCState =
                sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState;
            return test_helpers::makeCompletedTask(BMCState::Ready);
        });

        ON_CALL(*this, doFailoverImminentDelay()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, flushJournal()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, getFWVersion()).WillByDefault(Return("12345678"));

        ON_CALL(*this, waitForSelfPairing()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });
    }

    /**
     * @brief Run the registered system state callback for a given role.
     *
     * Allows tests to simulate system state transitions after the manager
     * has registered its callback.
     *
     * @param[in] role - The role whose callback to run
     * @param[in] state - The new system state to deliver
     */
    void runSystemStateCallback(Role role, SystemState state)
    {
        if (auto it = systemStateCBs.find(role); it != systemStateCBs.end())
        {
            it->second(state);
        }
    }

    /**
     * @brief Run the registered code update callback for a given role.
     *
     * Allows tests to simulate code update activation events after the
     * manager has registered its callback.
     *
     * @param[in] role - The role whose callback to run
     * @param[in] started - true if the update started, false if it failed
     */
    void runCodeUpdateCallback(Role role, bool started)
    {
        if (auto it = codeUpdateCBs.find(role); it != codeUpdateCBs.end())
        {
            it->second(started);
        }
    }
};

} // namespace rbmc
