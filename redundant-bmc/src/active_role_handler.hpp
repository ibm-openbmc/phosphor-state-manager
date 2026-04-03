// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "redundancy_mgr.hpp"
#include "role_handler.hpp"
#include "timer.hpp"

namespace rbmc
{

/**
 * @class ActiveRoleHandler
 *
 * This class handles operation specific to the active role.
 */
class ActiveRoleHandler : public RoleHandler
{
  public:
    ActiveRoleHandler(const ActiveRoleHandler&) = delete;
    ActiveRoleHandler& operator=(const ActiveRoleHandler&) = delete;
    ActiveRoleHandler(ActiveRoleHandler&&) = delete;
    ActiveRoleHandler& operator=(ActiveRoleHandler&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] providers - The Providers access object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    ActiveRoleHandler(sdbusplus::async::context& ctx, Providers& providers,
                      RedundancyInterface& iface) :
        RoleHandler(ctx, providers, iface), redMgr(ctx, providers, iface),
        siblingHealthTimer(
            ctx,
            std::bind_front(&ActiveRoleHandler::siblingHealthCritical, this)),
        peerConnectionTimer(
            ctx,
            std::bind_front(&ActiveRoleHandler::peerConnectionCritical, this))
    {}

    /**
     * @brief Destructor
     */
    ~ActiveRoleHandler() override
    {
        stopAllWatches();
    }

    /**
     * @brief Starts the handler.
     */
    sdbusplus::async::task<> start() override;

    /**
     * @brief Called when the DisableRedundancyOverride D-Bus property
     *        is updated.
     *
     * May disable or enable redundancy at this time if possible.
     *
     * @param[in] disable - If redundancy should be disabled
     *                      or enabled.
     */
    void disableRedPropChanged(bool disable) override
    {
        redMgr.disableRedPropChanged(disable);
    }

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

    /**
     * @brief Sets FailoversAllowed to false with the reason set to
     *        'failover in progress'.
     */
    inline void clearFailoversAllowedDuringFailover()
    {
        redMgr.clearFailoversAllowedDuringFailover();
    }

    /**
     * @brief Start the obmc-bmc-active systemd target and wait
     *        for it to complete.
     *
     * All active services will have been started when this returns.
     */
    sdbusplus::async::task<> failoverStartActiveTarget();

    /**
     * @brief Waits for the sibling to come back online after it was
     *        reset during a failover.
     */
    sdbusplus::async::task<> failoverWaitForSibling();

    /**
     * @brief Determines redundancy and failovers allowed after the
     *        new passive BMC has come back up (or timed out)
     *        during the failover.
     *
     * If redundancy is enabled, will issue a full sync.
     */
    sdbusplus::async::task<> failoverDetermineRedundancy();

  private:
    /**
     * @brief Starts all property watches/callbacks
     */
    inline void startAllWatches()
    {
        // Start sibling watches
        auto& sibling = providers.getSibling();
        sibling.addBMCStateCallback(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::siblingStateChange, this));
        sibling.addHealthCallback(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::siblingHealthChange, this));
        sibling.addFailoverImminentCallback(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::siblingFailoverImminent, this));

        // Start sync health watch
        providers.getSyncInterface().watchSyncHealth(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::syncHealthPropertyChanged,
                            this));

        // Start peer connected watch
        providers.getServices().addPeerConnectedCallback(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::peerConnectionChange, this));
    }

    /**
     * @brief Stops all property watches/callbacks
     */
    inline void stopAllWatches()
    {
        // Stop sibling watches
        siblingHealthTimer.stop();
        providers.getSibling().clearCallbacks(Role::Active);

        // Stop sync health watch
        providers.getSyncInterface().stopSyncHealthWatch(Role::Active);

        // Stop peer connected watch
        peerConnectionTimer.stop();
        providers.getServices().removePeerConnectedCallback(Role::Active);
    }

    using BMCState =
        sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState;

    /**
     * @brief Called when the sibling's BMC state changes
     *        assuming the callback has been enabled.
     *
     * @param[in] state - The new state value
     */
    void siblingStateChange(BMCState state);

    /**
     * @brief Called when the sibling's health changes
     *        assuming the callback has been enabled.
     *
     * Spawns siblingHealthy() when changes to good, and calls
     * siblingHealthCritical() when changes to bad.
     *
     * @param[in] alive - If sibling is alive and healthy
     */
    void siblingHealthChange(bool alive);

    /**
     * @brief Called when the sibling becomes good after
     *        sibling monitoring has been enabled.
     *
     * This will attempt to re-enable redundancy, though it might
     * not be possible for other reasons.
     */
    sdbusplus::async::task<> siblingHealthy();

    /**
     * @brief Called when the sibling health changes to bad
     *        long enough to explicitly disable redundancy.
     */
    void siblingHealthCritical();

    /**
     * @brief Called when the peer connection property changes
     *
     * Manages the peer connection timer and attempts recovery when
     * the connection is restored.
     *
     * @param[in] connected - The new value.
     */
    void peerConnectionChange(bool connected);

    /**
     * @brief Called when the peer connection stays bad
     *        long enough to explicitly disable redundancy.
     */
    void peerConnectionCritical();

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
     *
     * Redundancy will be disabled if it's a true sync failure.
     */
    sdbusplus::async::task<> syncHealthCritical();

    /**
     * @brief Called when the passive BMC is about to start a failover
     *        so that any preparation can be done.
     *
     * @param[in] imminent - The new FailoverImminent value.
     *
     */
    void siblingFailoverImminent(bool imminent);

    /**
     * @brief Redundancy manager object
     */
    RedundancyMgr redMgr;

    /**
     * @brief Timer used when the sibling's health changes to bad
     *
     * Upon expiration redundancy will be disabled.
     */
    Timer siblingHealthTimer;

    /**
     * @brief Timer used when the peer connection changes to bad
     *
     * Upon expiration redundancy will be disabled.
     */
    Timer peerConnectionTimer;
};

} // namespace rbmc
