/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "providers.hpp"
#include "redundancy_interface.hpp"
#include "role_determination.hpp"
#include "role_handler.hpp"

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Control/Failover/aserver.hpp>

namespace rbmc
{

/**
 * @class Manager
 *
 * Manages the high level operations of the redundant
 * BMC functionality.
 */
class Manager :
    public sdbusplus::aserver::xyz::openbmc_project::control::Failover<Manager>
{
  public:
    ~Manager() = default;
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] providers - The Providers access object
     */
    Manager(sdbusplus::async::context& ctx,
            std::unique_ptr<Providers>&& providers);

    /**
     * @brief Handler for the DisableRedundancyOverride
     *        property changing.
     *
     * Passes the value through to the role handler.
     *
     * @param[in] disable - The property value
     */
    void disableRedPropChanged(bool disable);

    /**
     * @brief Implements the StartFailover D-Bus method.
     *
     * @param[in] options - The failover options
     */
    sdbusplus::async::task<> method_call(start_failover_t /* unused */,
                                         const FailoverOptions& options);

  private:
    /**
     * @brief Kicks off the Manager startup
     */
    sdbusplus::async::task<> startup();

    /**
     * @brief Starts the heartbeat.
     */
    void startHeartbeat();

    /**
     * @brief Emits a heartbeat signal every second
     *
     * The sibling gets this via the sibling app to
     * know all's well.
     */
    sdbusplus::async::task<> doHeartBeat();

    /**
     * @brief Determines the BMC role
     *
     * @return roleInfo - The role + role error if any
     */
    role_determination::RoleInfo determineRole();

    /**
     * @brief Determine if the BMC must be passive due to a problem.
     *
     * If so, the role will be set before the heartbeat is started, so
     * the sibling BMC can know it should be active if possible.
     *
     * @return roleInfo - The passive role + error if must be passive.
     */
    sdbusplus::async::task<std::optional<role_determination::RoleInfo>>
        determinePassiveRoleIfRequired();

    /**
     * @brief Updates D-Bus with and serializes the new role
     *
     * @param[in] roleInfo - The new role and error if any
     */
    void updateRole(const role_determination::RoleInfo& roleInfo);

    /**
     * @brief Creates either an ActiveRoleHandler or
     *        PassiveRoleHandler object depending on
     *        the role and spawns handler->start().
     */
    void spawnRoleHandler();

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The Redundancy D-Bus interface
     */
    RedundancyInterface redundancyInterface;

    /**
     * @brief The role handler class
     */
    std::unique_ptr<RoleHandler> handler;

    /**
     * @brief Contains the various provider helpers
     */
    std::unique_ptr<Providers> providers;

    /**
     * @brief The previously serialized role value.
     *
     * Read from the filesystem in the constructor.
     */
    Role previousRole{Role::Unknown};

    /**
     * @brief If previous passive role was selected due to an error case.
     *
     * Read from the filesystem in the constructor.
     */
    bool chosePassiveDueToError{false};
};

} // namespace rbmc
