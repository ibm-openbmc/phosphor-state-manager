// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "role_handler.hpp"

namespace rbmc
{

/**
 * @class PassiveRoleHandler
 *
 * This class handles operation specific to the passive role.
 */
class PassiveRoleHandler : public RoleHandler
{
  public:
    PassiveRoleHandler() = delete;
    PassiveRoleHandler(const PassiveRoleHandler&) = delete;
    PassiveRoleHandler& operator=(const PassiveRoleHandler&) = delete;
    PassiveRoleHandler(PassiveRoleHandler&&) = delete;
    PassiveRoleHandler& operator=(PassiveRoleHandler&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] providers - The Providers access object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    PassiveRoleHandler(sdbusplus::async::context& ctx, Providers& providers,
                       RedundancyInterface& iface);

    /**
     * @brief Destructor
     *
     * Unregisters from callbacks
     */
    ~PassiveRoleHandler() override
    {
        stopAllWatches();
    }

    /**
     * @brief Stops all watches/callbacks registered by this handler.
     */
    void stopAllWatches() override
    {
        providers.getSibling().clearCallbacks(Role::Passive);
        providers.getSyncInterface().stopSyncHealthWatch(Role::Passive);
        providers.getServices().removePeerConnectedCallback(Role::Passive);
    }

    /**
     * @brief Starts the handler.
     */
    sdbusplus::async::task<> start() override;

    /**
     * @brief Called when a failover is requested, this will return
     *        Reason::none if a failover is allowed right now, or the
     *        reason that it isn't.
     *
     * @param[in] options - The options passed into the StartFailover
     *                      D-Bus method.
     *
     * @return Reason::none if failover is OK, else the reason it isn't.
     */
    sdbusplus::async::task<fo_blocked::Reason> getFailoverBlockedReason(
        const FailoverOptions& options) override;

  private:
    /**
     * @brief Setup mirroring the active BMC's
     *        RedundancyEnabled D-Bus property.
     */
    void setupSiblingRedEnabledWatch();

    /**
     * @brief Handler for the RedundancyEnabled property
     *        on the sibling's D-Bus interface changing.
     *
     * Will mirror the value on this BMC's Redundancy
     * interface if the other BMC is Active.
     */
    void siblingRedEnabledHandler(bool enable);

    /**
     * @brief Setup watching the sibling BMC's
     *        FailoversAllowed D-Bus property.
     */
    void setupSiblingFailoversAllowedWatch();

    /**
     * @brief Handler for the FailoversAllowed property
     *        on the sibling's D-Bus interface changing.
     *
     * Will mirror the value on this BMC's Redundancy
     * interface if the other BMC is Active.
     */
    void siblingFailoversAllowedHandler(bool allowed);

    /**
     * @brief Handler for the DisableRedundancyOverride
     *        property changing.
     *
     * Not supported on the passive BMC so an error will
     * be thrown.
     *
     * @param[in] disable - The new disable value.
     */
    void disableRedPropChanged(bool disable) override;

    /**
     * @brief Handler for external redundancy input changes
     *
     * Not supported on the passive BMC so an error will
     * be thrown.
     */
    void externalRedundancyInputChanged() override;

    /**
     * @brief Setup watching the sibling BMC's health
     */
    inline void setupSiblingHealthWatch()
    {
        providers.getSibling().addHealthCallback(
            Role::Passive,
            std::bind_front(&PassiveRoleHandler::siblingHealthChange, this));
    }

    /**
     * @brief Setup watching the PeerConnected property
     */
    inline void setupPeerConnectedWatch()
    {
        providers.getServices().addPeerConnectedCallback(
            Role::Passive,
            std::bind_front(&PassiveRoleHandler::peerConnectedChange, this));
    }

    /**
     * @brief Kicks off a full sync if conditions are right.
     *
     * Otherwise, stops the background sync.
     */
    sdbusplus::async::task<> tryFullSync();

    /**
     * @brief Does a full sync and then enables the sync health watch
     *
     * If a full sync has already been done since sync was last stopped,
     * it will skip it.
     **/
    sdbusplus::async::task<> startSync();

    /**
     * @brief Stops background syncing
     */
    sdbusplus::async::task<> stopSync();

    /**
     * @brief Called when the sibling BMC health changes.
     *
     * Will try to start or stop syncing as appropriate.
     */
    void siblingHealthChange(bool alive);

    /**
     * @brief Called when the sync health property changes
     *
     * Spawns syncHealthCritical() on critical health.
     *
     * @param[in] health - The new health value
     */
    void syncHealthPropertyChanged(SyncBMCData::SyncEventsHealth health);

    /**
     * @brief Spawned when the sync health changes to critical
     *        to stop background sync if it was running.
     */
    sdbusplus::async::task<> syncHealthCritical();

    /**
     * @brief Called when the PeerConnected property changes.
     *
     * Tries to either start or stop a sync.
     */
    void peerConnectedChange(bool connected);

    /**
     * @brief Tracks if a full sync has already been done since
     *        the last time sync was stopped.
     *
     * This makes it easier to deal with both the heartbeat and
     * enabled properties being used as a sync trigger.  It can
     * then just try on each one.
     */
    bool fullSyncDone{false};
};

} // namespace rbmc
