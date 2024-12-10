// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "redundancy_interface.hpp"
#include "services.hpp"
#include "sibling.hpp"

namespace rbmc
{
using Role =
    sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy::Role;

/**
 * @class RoleHandler
 *
 * This is the base class for the active and passive role
 * handler classes, which will handle all of the role specific
 * code.
 */
class RoleHandler
{
  public:
    RoleHandler() = delete;
    virtual ~RoleHandler() = default;
    RoleHandler(const RoleHandler&) = delete;
    RoleHandler& operator=(const RoleHandler&) = delete;
    RoleHandler(RoleHandler&&) = delete;
    RoleHandler& operator=(RoleHandler&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] services - The services object
     * @param[in] sibling - The sibling object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    RoleHandler(sdbusplus::async::context& ctx, Services& services,
                Sibling& sibling, RedundancyInterface& iface) :
        ctx(ctx), services(services), sibling(sibling),
        redundancyInterface(iface)
    {}

    /**
     * @brief Pure virtual function to start the handler
     *        in an async context.
     */
    virtual sdbusplus::async::task<> start() = 0;

    /**
     * @brief Handler for the RedundancyEnabled property
     *        on the sibling's D-Bus interface changing.
     */
    virtual void siblingRedEnabledHandler(bool /*enable*/) {};

  protected:
    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The services object
     */
    Services& services;

    /**
     * @brief The Sibling object
     */
    Sibling& sibling;

    /**
     * @brief The Redundancy D-Bus interface
     */
    RedundancyInterface& redundancyInterface;
};

} // namespace rbmc
