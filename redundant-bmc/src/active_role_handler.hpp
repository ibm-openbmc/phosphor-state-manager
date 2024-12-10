// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "redundancy.hpp"
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
        manualDisable(iface.disable_redundancy_override())
    {}

    /**
     * @brief Starts the handler.
     */
    sdbusplus::async::task<> start() override;

  private:
    /**
     * @brief Enables or disables redundancy based on the
     *        system config.
     */
    void determineAndSetRedundancy();

    /**
     * @brief Returns the reasons that redundancy cannnot be
     *        enabled, if any.
     *
     * @return The reasons.  If empty, then redundancy can be enabled.
     */
    redundancy::NoRedundancyReasons getNoRedundancyReasons();

    /**
     * @brief Based on if disableReasons is empty or not, disables
     *        or enables redundancy.
     *
     * @param[in] disableReasons - The reasons redundancy must be
     *            disabled, if any.
     */
    void enableOrDisableRedundancy(
        const redundancy::NoRedundancyReasons& disableReasons);

    /**
     * @brief If determineAndSetRedundancy() has ran yet or not.
     */
    bool redundancyDetermined = false;

    /**
     * @brief If redundancy has been manually disabled via the
     *        D-Bus property.
     */
    bool manualDisable = false;
};

} // namespace rbmc
