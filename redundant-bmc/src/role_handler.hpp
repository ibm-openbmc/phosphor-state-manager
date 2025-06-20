// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "providers.hpp"
#include "redundancy.hpp"
#include "redundancy_interface.hpp"

using FailoverOptions = std::map<std::string, std::variant<bool>>;

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
     * @param[in] providers - The Providers access object
     * @param[in] iface - The redundancy D-Bus interface object
     */
    RoleHandler(sdbusplus::async::context& ctx, Providers& providers,
                RedundancyInterface& iface) :
        ctx(ctx), providers(providers), redundancyInterface(iface)
    {}

    /**
     * @brief Pure virtual function to start the handler
     *        in an async context.
     */
    virtual sdbusplus::async::task<> start() = 0;

    /**
     * @brief Handler for the DisableRedundancyOverride
     *        property changing.
     *
     * @param[in] disable - The new disable value.
     */
    virtual void disableRedPropChanged(bool disable) = 0;

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
    virtual sdbusplus::async::task<fo_blocked::Reason> getFailoverBlockedReason(
        const FailoverOptions& options) = 0;

  protected:
    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief Provides the various system interfaces
     */
    Providers& providers;

    /**
     * @brief The Redundancy D-Bus interface
     */
    RedundancyInterface& redundancyInterface;
};

} // namespace rbmc
