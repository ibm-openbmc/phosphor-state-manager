// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Control/SyncBMCData/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>

namespace rbmc
{
using SyncBMCData =
    sdbusplus::client::xyz::openbmc_project::control::SyncBMCData<>;

/**
 * @class SyncInterface
 *
 * The interface to the sync daemon to start and stop syncs.
 */
class SyncInterface
{
  public:
    SyncInterface() = default;
    virtual ~SyncInterface() = default;
    SyncInterface(const SyncInterface&) = delete;
    SyncInterface& operator=(const SyncInterface&) = delete;
    SyncInterface(SyncInterface&&) = delete;
    SyncInterface& operator=(SyncInterface&&) = delete;

    /**
     * @brief Starts a full sync and waits for it to finish.
     *
     * @return true if it completely successfully, else false
     */
    virtual sdbusplus::async::task<bool> doFullSync() = 0;

    /**
     * @brief Turns off background syncing
     */
    virtual sdbusplus::async::task<> disableBackgroundSync() = 0;

    /**
     * @brief Says if a full sync is running inside doFullSync().
     */
    inline bool isFullSyncInProgress() const
    {
        return fullSyncInProgress;
    }

    using SyncHealthCallback =
        std::function<void(SyncBMCData::SyncEventsHealth)>;

    using Role =
        sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy::Role;

    /**
     * @brief Adds a function to call when the sync health property
     *        changes.
     *
     * @param[in] role - The role to register with
     * @param[in] cb - The function object
     */
    void watchSyncHealth(Role role, SyncHealthCallback&& cb)
    {
        healthCallbacks.emplace(role, std::move(cb));
    }

    /**
     * @brief Stops sync health callbacks
     *
     * @param[in] role - The role used to add the function.
     */
    void stopSyncHealthWatch(Role role)
    {
        healthCallbacks.erase(role);
    }

  protected:
    /**
     * @brief If doFullSync is in the middle of doing a sync
     */
    bool fullSyncInProgress = false;

    /**
     * @brief The health status callback functions
     */
    std::map<Role, SyncHealthCallback> healthCallbacks;
};

}; // namespace rbmc
