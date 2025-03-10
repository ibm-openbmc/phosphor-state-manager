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
     * @param[in] services - The services object
     * @param[in] sibling - The sibling object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    ActiveRoleHandler(sdbusplus::async::context& ctx, Services& services,
                      Sibling& sibling, RedundancyInterface& iface) :
        RoleHandler(ctx, services, sibling, iface),
        redMgr(ctx, services, sibling, iface),
        siblingHBTimer(
            ctx, std::bind_front(&ActiveRoleHandler::siblingHBCritical, this))
    {}

    /**
     * @brief Destructor
     */
    ~ActiveRoleHandler() override
    {
        sibling.clearCallbacks(Role::Active);
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

  private:
    /**
     * @brief Starts the Sibling property watches/callbacks
     */
    inline void startSiblingWatches()
    {
        sibling.addBMCStateCallback(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::siblingStateChange, this));

        sibling.addHeartbeatCallback(
            Role::Active,
            std::bind_front(&ActiveRoleHandler::siblingHBChange, this));
    }

    /**
     * @brief Stops the sibling property callbacks/watches
     */
    inline void stopSiblingWatches()
    {
        siblingHBTimer.stop();
        sibling.clearBMCStateCallback(Role::Active);
        sibling.clearHeartbeatCallback(Role::Active);
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
     * @brief Called when the sibling's heartbeat changes
     *        assuming the callback has been enabled.
     *
     * Spawns siblingHBStarted() on a start, and calls
     * siblingHBCritical() on a stop.
     *
     * @param[in] hb - The new heartbeat status
     */
    void siblingHBChange(bool hb);

    /**
     * @brief Called when the sibling heartbeat starts after
     *        sibling monitoring has been enabled.
     *
     * This will attempt to re-enable redundancy, though it might
     * not be possible for other reasons.
     */
    sdbusplus::async::task<> siblingHBStarted();

    /**
     * @brief Called when the sibling heartbeat has been stopped
     *        long enough to explicitly disable redundancy.
     */
    void siblingHBCritical();

    /**
     * @brief Redundancy manager object
     */
    RedundancyMgr redMgr;

    /**
     * @brief Timer used when the sibling's heartbeat stops
     *
     * Upon expiration redundancy will be disabled.
     */
    Timer siblingHBTimer;
};

} // namespace rbmc
