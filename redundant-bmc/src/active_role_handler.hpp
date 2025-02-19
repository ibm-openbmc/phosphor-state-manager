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
    ActiveRoleHandler() = delete;
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
     * @brief Redundancy manager object
     */
    RedundancyMgr redMgr;
};

} // namespace rbmc
