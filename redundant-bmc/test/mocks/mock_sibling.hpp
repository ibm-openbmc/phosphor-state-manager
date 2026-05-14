// SPDX-License-Identifier: Apache-2.0
#pragma once

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
};

} // namespace rbmc
