// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "mock_services.hpp"
#include "mock_sibling.hpp"
#include "mock_sibling_reset.hpp"
#include "mock_sync_interface.hpp"
#include "providers.hpp"

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

  private:
    MockServices mockServices;
    MockSibling mockSibling;
    MockSyncInterface mockSyncInterface;
    MockSiblingReset mockSiblingReset;
};

} // namespace rbmc
