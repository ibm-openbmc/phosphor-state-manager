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
     * @param[in] services - The services object
     * @param[in] sibling - The sibling object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    PassiveRoleHandler(sdbusplus::async::context& ctx, Services& services,
                       Sibling& sibling, RedundancyInterface& iface) :
        RoleHandler(ctx, services, sibling, iface)
    {}

    /**
     * @brief Destructor
     *
     * Unregisters from callbacks
     */
    ~PassiveRoleHandler() override
    {
        sibling.clearCallbacks(Role::Passive);
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
    void siblingRedEnabledHandler(bool enable) override;

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
};

} // namespace rbmc
