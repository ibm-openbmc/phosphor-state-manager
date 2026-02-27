// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "persistent_data.hpp"

#include <filesystem>

#include <gtest/gtest.h>

namespace rbmc::test
{

/**
 * @class PersistentDataTestFixture
 *
 * Creates and destroys the persistent data directory
 * around testcases.
 *
 * Tests that need persistent data functionality should inherit from this.
 */
class PersistentDataTestFixture : public ::testing::Test
{
  protected:
    PersistentDataTestFixture()
    {
        char tempDir[] = "/tmp/rbmc_data_test_XXXXXX";
        dataDir = mkdtemp(tempDir);

        data::setDataDirectory(dataDir);
    }

    ~PersistentDataTestFixture() override
    {
        if (!dataDir.empty())
        {
            std::filesystem::remove_all(dataDir);
        }
    }

    std::filesystem::path dataDir;
};

} // namespace rbmc::test
