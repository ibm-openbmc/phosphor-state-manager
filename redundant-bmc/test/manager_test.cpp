// SPDX-License-Identifier: Apache-2.0
#include "mocks/mock_providers.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace rbmc;

class ManagerTest
{
  protected:
    void SetUp()
    {
        mockProviders = std::make_unique<MockProviders>();
    }

    void TearDown()
    {
        mockProviders.reset();
    }

    std::unique_ptr<MockProviders> mockProviders;
};
