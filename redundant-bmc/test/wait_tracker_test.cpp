// SPDX-License-Identifier: Apache-2.0
#include "wait_tracker.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

namespace rbmc
{

class WaitTrackerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        std::string templatePath = (std::filesystem::temp_directory_path() /
                                    "wait_tracker_test_XXXXXX")
                                       .string();
        char* tempDir = mkdtemp(templatePath.data());
        if (tempDir == nullptr)
        {
            throw std::runtime_error("Failed to create temp directory");
        }
        testDir = tempDir;
        testFile = testDir / "wait_status.json";
    }

    void TearDown() override
    {
        std::filesystem::remove_all(testDir);
    }

    std::filesystem::path testDir;
    std::filesystem::path testFile;
};

TEST_F(WaitTrackerTest, DisabledWhenPathEmpty)
{
    WaitTracker tracker("");

    // Should not create any files
    EXPECT_FALSE(std::filesystem::exists(testFile));

    // Guard should work but not write anything
    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{10});
    }

    EXPECT_FALSE(std::filesystem::exists(testFile));
}

TEST_F(WaitTrackerTest, SingleWaitRegistration)
{
    WaitTracker tracker(testDir);

    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{60});

        // File should exist
        ASSERT_TRUE(std::filesystem::exists(testFile));

        // Read and verify
        auto waits = WaitTracker::readWaits(testDir);
        ASSERT_EQ(waits.size(), 1);
        EXPECT_EQ(waits[0].operation, WaitOperation::peerConnection);
        EXPECT_EQ(waits[0].timeoutSeconds, 60);
        EXPECT_GT(waits[0].startTimeMs, 0);
    }

    // After guard destroyed, wait should be removed
    auto waits = WaitTracker::readWaits(testDir);
    EXPECT_EQ(waits.size(), 0);
}

TEST_F(WaitTrackerTest, MultipleParallelWaits)
{
    WaitTracker tracker(testDir);

    WaitTracker::WaitGuard guard1(tracker, WaitOperation::peerConnection,
                                  std::chrono::seconds{60});
    WaitTracker::WaitGuard guard2(tracker, WaitOperation::siblingAlive,
                                  std::chrono::seconds{360});
    WaitTracker::WaitGuard guard3(tracker, WaitOperation::systemInventoryPath,
                                  std::chrono::seconds{180});

    auto waits = WaitTracker::readWaits(testDir);
    ASSERT_EQ(waits.size(), 3);

    // Verify all three operations are present
    std::set<WaitOperation> ops;
    for (const auto& wait : waits)
    {
        ops.insert(wait.operation);
    }

    EXPECT_TRUE(ops.contains(WaitOperation::peerConnection));
    EXPECT_TRUE(ops.contains(WaitOperation::siblingAlive));
    EXPECT_TRUE(ops.contains(WaitOperation::systemInventoryPath));
}

TEST_F(WaitTrackerTest, WaitOperationToString)
{
    EXPECT_EQ(waitOperationToString(WaitOperation::peerConnection),
              "PeerConnection");
    EXPECT_EQ(waitOperationToString(WaitOperation::siblingAlive),
              "SiblingAlive");
    EXPECT_EQ(waitOperationToString(WaitOperation::systemInventoryPath),
              "SystemInventoryPath");
    EXPECT_EQ(waitOperationToString(WaitOperation::siblingHealthTimer),
              "SiblingHealthTimer");
}

TEST_F(WaitTrackerTest, ReadNonexistentFile)
{
    auto waits = WaitTracker::readWaits(testDir);
    EXPECT_EQ(waits.size(), 0);
}

TEST_F(WaitTrackerTest, ReadCorruptedFile)
{
    // Write invalid JSON
    std::ofstream ofs(testFile);
    ofs << "not valid json{{{";
    ofs.close();

    auto waits = WaitTracker::readWaits(testDir);
    EXPECT_EQ(waits.size(), 0);
}

TEST_F(WaitTrackerTest, ElapsedTimeCalculation)
{
    WaitTracker tracker(testDir);

    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{60});

        // Wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto waits = WaitTracker::readWaits(testDir);
        ASSERT_EQ(waits.size(), 1);

        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        auto elapsedMs = nowMs - waits[0].startTimeMs;

        // Should be at least 10ms
        EXPECT_GE(elapsedMs, 10);
    }
}

TEST_F(WaitTrackerTest, SequentialWaits)
{
    WaitTracker tracker(testDir);

    // First wait
    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{60});
        auto waits = WaitTracker::readWaits(testDir);
        EXPECT_EQ(waits.size(), 1);
    }

    // After first wait completes, should be empty
    auto waits = WaitTracker::readWaits(testDir);
    EXPECT_EQ(waits.size(), 0);

    // Second wait
    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::siblingAlive,
                                     std::chrono::seconds{360});
        waits = WaitTracker::readWaits(testDir);
        EXPECT_EQ(waits.size(), 1);
        EXPECT_EQ(waits[0].operation, WaitOperation::siblingAlive);
    }

    // After second wait completes, should be empty again
    waits = WaitTracker::readWaits(testDir);
    EXPECT_EQ(waits.size(), 0);
}

TEST_F(WaitTrackerTest, FileContainsCorrectJSON)
{
    WaitTracker tracker(testDir);

    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{120});

        // Read file directly and verify JSON structure
        std::ifstream ifs(testFile);
        ASSERT_TRUE(ifs.is_open());

        nlohmann::json j;
        ifs >> j;

        ASSERT_TRUE(j.contains("waits"));
        ASSERT_TRUE(j["waits"].is_array());
        ASSERT_EQ(j["waits"].size(), 1);

        auto& wait = j["waits"][0];
        EXPECT_TRUE(wait.contains("id"));
        EXPECT_TRUE(wait.contains("operationEnum"));
        EXPECT_TRUE(wait.contains("startTimeMs"));
        EXPECT_TRUE(wait.contains("timeoutSeconds"));

        EXPECT_EQ(wait["timeoutSeconds"].get<uint32_t>(), 120);
    }
}

TEST_F(WaitTrackerTest, ConstructorClearsStaleData)
{
    // Simulate stale data from a previous run by creating a file with waits
    {
        WaitTracker tracker1(testDir);
        WaitTracker::WaitGuard guard(tracker1, WaitOperation::peerConnection,
                                     std::chrono::seconds{60});

        // Verify wait is registered
        auto waits = WaitTracker::readWaits(testDir);
        ASSERT_EQ(waits.size(), 1);
    }
    // Guard destroyed but file still has data

    // Create new tracker - should clear stale data
    WaitTracker tracker2(testDir);

    // File should be removed when no active waits
    EXPECT_FALSE(std::filesystem::exists(testFile));
    auto waits = WaitTracker::readWaits(testDir);
    EXPECT_EQ(waits.size(), 0);
}

TEST_F(WaitTrackerTest, EnableTrackingAfterConstruction)
{
    WaitTracker tracker("");

    EXPECT_FALSE(std::filesystem::exists(testFile));

    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{60});
        EXPECT_FALSE(std::filesystem::exists(testFile));
    }

    tracker.enableTracking(testDir);

    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::siblingAlive,
                                     std::chrono::seconds{120});
        EXPECT_TRUE(std::filesystem::exists(testFile));

        auto waits = WaitTracker::readWaits(testDir);
        ASSERT_EQ(waits.size(), 1);
        EXPECT_EQ(waits[0].operation, WaitOperation::siblingAlive);
        EXPECT_EQ(waits[0].timeoutSeconds, 120);
    }
}

TEST_F(WaitTrackerTest, EnableTrackingWithEmptyPath)
{
    WaitTracker tracker("");

    tracker.enableTracking("");

    {
        WaitTracker::WaitGuard guard(tracker, WaitOperation::peerConnection,
                                     std::chrono::seconds{60});
        EXPECT_FALSE(std::filesystem::exists(testFile));
    }
}

} // namespace rbmc
