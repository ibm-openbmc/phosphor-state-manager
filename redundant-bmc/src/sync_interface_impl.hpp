// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "sync_interface.hpp"

namespace rbmc
{

/**
 * @class SyncInterfaceImpl
 *
 * The interface to the sync daemon to start and stop syncs.
 */
class SyncInterfaceImpl : public SyncInterface
{
  public:
    ~SyncInterfaceImpl() override = default;
    SyncInterfaceImpl(const SyncInterfaceImpl&) = delete;
    SyncInterfaceImpl& operator=(const SyncInterfaceImpl&) = delete;
    SyncInterfaceImpl(SyncInterfaceImpl&&) = delete;
    SyncInterfaceImpl& operator=(SyncInterfaceImpl&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     */
    explicit SyncInterfaceImpl(sdbusplus::async::context& ctx) : ctx(ctx)
    {
        ctx.spawn(watchSyncEventsHealthPropertyChanged());
    }

    /**
     * @brief Starts a full sync and waits for it to finish.
     *
     * @return true if it completely successfully, else false
     */
    sdbusplus::async::task<bool> doFullSync() override;

    /**
     * @brief Turns off background syncing
     */
    sdbusplus::async::task<> disableBackgroundSync() override;

  private:
    /**
     * @brief Looks up the data sync D-Bus service if it hasn't already
     *        been looked up.
     */
    sdbusplus::async::task<> lookupService();

    /**
     * @brief The properties changed watch for the sync health property
     */
    sdbusplus::async::task<> watchSyncEventsHealthPropertyChanged();

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The name of the sync daemon's D-Bus service.
     */
    std::string syncService;
};

}; // namespace rbmc
