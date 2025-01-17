// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "redundancy_mgr.hpp"
#include "role_handler.hpp"

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
        redMgr(ctx, services, sibling, iface)
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
    }

    /**
     * @brief Stops the sibling property callbacks/watches
     */
    inline void stopSiblingWatches()
    {
        sibling.clearBMCStateCallback(Role::Active);
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
     * @brief Redundancy manager object
     */
    RedundancyMgr redMgr;
};

} // namespace rbmc
