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
 * Base test fixture that provides temporary directory management
 * for persistent data testing.
 *
 * This fixture:
 * - Creates a temporary directory in SetUp()
 * - Configures data::setDataDirectory() to use the temp directory
 * - Cleans up the temp directory in TearDown()
 *
 * Tests that need persistent data functionality should inherit from this.
 */
class PersistentDataTestFixture : public ::testing::Test
{
  protected:
    PersistentDataTestFixture()
    {
        // Create temporary directory for persistent data
        char tempDir[] = "/tmp/rbmc_data_test_XXXXXX";
        dataDir = mkdtemp(tempDir);

        // Set the data directory for the data namespace
        data::setDataDirectory(dataDir);
    }

    ~PersistentDataTestFixture() override
    {
        // Clean up test data directory
        if (!dataDir.empty())
        {
            std::filesystem::remove_all(dataDir);
        }
    }

    std::filesystem::path dataDir;
};

} // namespace rbmc::test
