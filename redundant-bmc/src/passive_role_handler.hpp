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
                       RedundancyInterface& iface) :
        RoleHandler(ctx, providers, iface)
    {}

    /**
     * @brief Destructor
     *
     * Unregisters from callbacks
     */
    ~PassiveRoleHandler() override
    {
        providers.getSibling().clearCallbacks(Role::Passive);
        providers.getSyncInterface().stopSyncHealthWatch(Role::Passive);
    }

    /**
     * @brief Starts the handler.
     */
    sdbusplus::async::task<> start() override;

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
     * @brief Setup watching the sibling BMC's heartbeat
     */
    inline void setupSiblingHBWatch()
    {
        providers.getSibling().addHeartbeatCallback(
            Role::Passive, [this](bool hb) { siblingHBChange(hb); });
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
     * @brief Called when the sibling BMC heartbeat changes.
     *
     * Will try to start or stop syncing as appropriate.
     */
    void siblingHBChange(bool hb);

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
