// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rbmc
{

enum class WaitOperation
{
    systemInventoryPath,
    systemInventoryStatus,
    peerConnection,
    selfPairing,
    startUnit,
    siblingAlive,
    siblingBMCSteadyState,
    siblingHealthTimer,
    peerConnectionTimer,
    bmcResetTimer,
    fullSync
};

/**
 * @brief Convert the enum to a human readable string
 *
 * @param[in] op - The operation
 *
 * @return std::string - The string version
 */
std::string waitOperationToString(WaitOperation op);

class WaitTracker
{
  public:
    struct WaitInfo
    {
        WaitOperation operation;
        uint64_t startTimeMs; // epoch milliseconds
        uint32_t timeoutSeconds;
    };

    /**
     * @brief Constructor
     *
     * Tracking isn't enabled if an empty path is passed in, and can
     * then be enabled later with enableTracking.
     *
     * @param dirPath Directory path where wait_status.json will be created.
     */
    explicit WaitTracker(const std::filesystem::path& dirPath = "");

    /**
     * @brief Enable tracking by setting the directory path
     *
     * Necessary if the path wasn't used during construction.
     *
     * @param dirPath Directory path where wait_status.json will be created
     */
    void enableTracking(const std::filesystem::path& dirPath);

    /**
     * @brief RAII guard that auto-registers/unregisters waits around
     *        its lifetime.
     */
    class WaitGuard
    {
      public:
        /**
         * @brief Constructor
         *
         * @param[in] tracker - The WaitTracker object
         * @param[in] operation - The wait operation
         * @param[in] timeout - The timeout for this wait
         */
        WaitGuard(WaitTracker& tracker, WaitOperation operation,
                  std::chrono::seconds timeout) :
            tracker(tracker), id(tracker.registerWait(operation, timeout))
        {}

        /**
         * @brief Unregisters the wait on destruction
         */
        ~WaitGuard()
        {
            tracker.unregisterWait(id);
        }

        WaitGuard(const WaitGuard&) = delete;
        WaitGuard& operator=(const WaitGuard&) = delete;
        WaitGuard(WaitGuard&&) = delete;
        WaitGuard& operator=(WaitGuard&&) = delete;

      private:
        WaitTracker& tracker;
        uint64_t id;
    };

    /**
     * @brief Read current waits from file
     *
     * Used by rbmctool.
     *
     * @param dirPath Directory path where wait_status.json is located
     *
     * @return Vector of active waits
     */
    static std::vector<WaitInfo> readWaits(
        const std::filesystem::path& dirPath);

  private:
    /**
     * @brief Registers the start of a wait
     *
     * @param[in] operation - The wait operation
     * @param[in] timeout - The wait timeout
     * @return The wait ID (0 if tracking disabled)
     */
    uint64_t registerWait(WaitOperation operation,
                          std::chrono::seconds timeout);

    /**
     * @brief Unregisters the wait (it's complete)
     *
     * @param[in] id - The ID of the wait to unregister
     */
    void unregisterWait(uint64_t id);

    /**
     * @brief Updates the underlying file with all active waits
     */
    void updateFile();

    /**
     * @brief The file where waits are persisted
     */
    std::filesystem::path filePath;

    /**
     * @brief The active waits
     *
     * The key is just an incrementing ID
     */
    std::map<uint64_t, WaitInfo> activeWaits;

    /**
     * @brief If wait tracking is enabled
     */
    bool enabled{false};

    /**
     * @brief Used for generating the IDs.
     */
    uint64_t counter{1};
};

} // namespace rbmc
