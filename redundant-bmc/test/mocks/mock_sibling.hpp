// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "async_helpers.hpp"
#include "sibling.hpp"

#include <gmock/gmock.h>

namespace rbmc
{

/**
 * @class MockSibling
 *
 * Mock implementation of the Sibling interface for testing.
 */
class MockSibling : public testing::NiceMock<Sibling>
{
  public:
    MockSibling() = default;
    ~MockSibling() override = default;

    MOCK_METHOD(sdbusplus::async::task<>, init, (), (override));

    MOCK_METHOD(bool, isBMCPresent, (), (override));
    MOCK_METHOD(bool, alive, (), (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, waitForSiblingUp, (), (override));
    MOCK_METHOD(sdbusplus::async::task<>, waitForSiblingRole, (), (override));
    MOCK_METHOD(sdbusplus::async::task<>, waitForBMCSteadyState, (),
                (const, override));

    MOCK_METHOD(std::optional<Role>, getRole, (), (const, override));
    MOCK_METHOD(std::optional<bool>, getRedundancyEnabled, (),
                (const, override));
    MOCK_METHOD(std::optional<bool>, getPaired, (), (const, override));
    MOCK_METHOD(std::optional<std::string>, getFWVersion, (),
                (const, override));
    MOCK_METHOD(std::optional<bool>, getFailoversAllowed, (),
                (const, override));
    MOCK_METHOD(std::optional<bool>, getFailoverInProgress, (),
                (const, override));
    MOCK_METHOD(std::optional<bool>, getFailoverImminent, (),
                (const, override));
    MOCK_METHOD(std::optional<bool>, getHasReasonForNoRedundancy, (),
                (const, override));
    MOCK_METHOD(std::optional<BMCState>, getBMCState, (), (const, override));

    MOCK_METHOD(const std::string&, getServiceName, (), (const, override));

    MOCK_METHOD(sdbusplus::async::task<>, pauseForHeartbeatChange, (),
                (const, override));
    MOCK_METHOD(sdbusplus::async::task<>, pauseForDataPropagation, (),
                (const, override));

    MOCK_METHOD(
        sdbusplus::async::task<>, startFailover,
        (sdbusplus::common::xyz::openbmc_project::control::Failover::Requester,
         const FailoverOptions&),
        (override));

    /**
     * @brief Setup default behaviors for common test scenarios to save
     *        setup in the testcases.
     */
    void setupDefaultBehavior()
    {
        using ::testing::_;
        using ::testing::Return;
        using ::testing::ReturnRef;

        ON_CALL(*this, init()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, getServiceName()).WillByDefault(ReturnRef(serviceName));

        ON_CALL(*this, waitForSiblingUp()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, waitForSiblingRole()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, waitForBMCSteadyState()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, pauseForHeartbeatChange()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, pauseForDataPropagation()).WillByDefault([]() {
            return test_helpers::makeCompletedTask();
        });

        ON_CALL(*this, getBMCState()).WillByDefault([this]() {
            if (this->alive())
            {
                return std::make_optional(BMCState::Ready);
            }
            return std::optional<BMCState>{};
        });

        ON_CALL(*this, getFWVersion()).WillByDefault([this]() {
            if (this->alive())
            {
                return std::make_optional<std::string>("12345678");
            }
            return std::optional<std::string>{};
        });
    }

  private:
    std::string serviceName{"xyz.openbmc_project.State.BMC.Redundancy.Sibling"};
};

} // namespace rbmc
