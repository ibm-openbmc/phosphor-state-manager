// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pcie_storage.hpp"

#include <gmock/gmock.h>

namespace rbmc
{

/**
 * @class MockPCIeStorage
 *
 * Mock implementation of PCIeStorage for testing.
 */
class MockPCIeStorage : public testing::NiceMock<pcie_data::PCIeStorage>
{
  public:
    MOCK_METHOD(pcie_data::RedundancyState, readState, (), (override));
    MOCK_METHOD(void, writeState, (const pcie_data::RedundancyState&),
                (override));
    MOCK_METHOD(void, updateRole, (uint8_t), (override));
    MOCK_METHOD(void, updateRedundancyEnabled, (bool), (override));
    MOCK_METHOD(void, updateFailoverInProgress, (bool), (override));
    MOCK_METHOD(void, updateFailoversAllowed, (bool), (override));
};

} // namespace rbmc
