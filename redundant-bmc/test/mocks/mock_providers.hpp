// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "mock_pcie_storage.hpp"
#include "mock_services.hpp"
#include "mock_sibling.hpp"
#include "mock_sibling_reset.hpp"
#include "mock_sync_interface.hpp"
#include "providers.hpp"
#include "test_progress_tracker.hpp"

#include <gmock/gmock.h>

namespace rbmc
{

/**
 * @class MockProviders
 *
 * Mock implementation of the Providers interface that holds
 * all the mock provider objects for testing.
 */
class MockProviders : public Providers
{
  public:
    MockProviders() = default;
    ~MockProviders() override = default;

    Services& getServices() override
    {
        return mockServices;
    }

    Sibling& getSibling() override
    {
        return mockSibling;
    }

    SyncInterface& getSyncInterface() override
    {
        return mockSyncInterface;
    }

    SiblingReset& getSiblingReset() override
    {
        return mockSiblingReset;
    }

    pcie_data::PCIeStorage* getPCIeStorage() override
    {
        return &mockPCIeStorage;
    }

    ProgressTracker& getTracker() override
    {
        return testProgressTracker;
    }

    // Helpers to get the Mock versions
    MockServices& getMockServices()
    {
        return mockServices;
    }

    MockSibling& getMockSibling()
    {
        return mockSibling;
    }

    MockSyncInterface& getMockSyncInterface()
    {
        return mockSyncInterface;
    }

    MockSiblingReset& getMockSiblingReset()
    {
        return mockSiblingReset;
    }

    MockPCIeStorage& getMockPCIeStorage()
    {
        return mockPCIeStorage;
    }

    TestProgressTracker& getTestTracker()
    {
        return testProgressTracker;
    }

  private:
    MockServices mockServices;
    MockSibling mockSibling;
    MockSyncInterface mockSyncInterface;
    MockSiblingReset mockSiblingReset;
    MockPCIeStorage mockPCIeStorage;
    TestProgressTracker testProgressTracker;
};

} // namespace rbmc
