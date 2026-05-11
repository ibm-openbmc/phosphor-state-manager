/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "providers.hpp"
#include "redundancy_interface.hpp"
#include "role_determination.hpp"
#include "role_handler.hpp"
#include "types.hpp"

#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Control/Failover/aserver.hpp>

namespace rbmc
{

using Requester =
    sdbusplus::common::xyz::openbmc_project::control::Failover::Requester;

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
     * @param[in] heartbeatInterval - Interval between heartbeats (default: 1s)
     */
    Manager(sdbusplus::async::context& ctx,
            std::unique_ptr<Providers>&& providers,
            std::chrono::milliseconds heartbeatInterval = std::chrono::seconds{
                1});

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
     * @brief Handler for external redundancy input changes
     *
     * Called when an external application signals a hardware
     * configuration issue that should disable redundancy.
     *
     * @param[in] input - The external input type
     * @param[in] value - The value to set it to
     */
    void setExternalRedundancyInput(RedundancyInterface::RedundancyInput input,
                                    bool value);

    /**
     * @brief Implements the StartFailover D-Bus method.
     *
     * @param[in] requester - For logging who requested the failover
     * @param[in] options - The failover options
     */
    sdbusplus::async::task<> method_call(start_failover_t /* unused */,
                                         Requester requester,
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
    sdbusplus::async::task<role_determination::RoleInfo> determineRole();

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
     * @brief Drives the failover from passive to active when
     *        this BMC is the starting passive one.
     *
     * @param[in] requester - The failover requester passed into the
     *                        StartFailover D-Bus method.
     */
    sdbusplus::async::task<> doFailoverFromPassive(Requester requester);

    /**
     * @brief Clears 'failover in progress' if it is on and
     *        removes the persisted value.
     */
    sdbusplus::async::task<> postStartupClearFOInProgress();

    /**
     * @brief Checks if a failover can be started now.
     *
     * @param[in] options - The options passed to StartFailover
     *
     * @return std::string - The reason for the failover rejection
     *                       or empty string if it can proceed.
     *
     */
    sdbusplus::async::task<fo_blocked::Reason> validateFailoverRequest(
        const FailoverOptions& options);

    /**
     * @brief Setup watch for pairing changes
     */
    void setupPairedWatch();

    /**
     * @brief Handler for paired property changes
     *
     * @param[in] paired - The new paired value
     */
    void pairedChangeHandler(bool paired);

    /**
     * @brief Async handler spawned by pairedChangeHandler
     *
     * @param[in] paired - The new paired value
     */
    sdbusplus::async::task<> handlePairedChange(bool paired);

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The Redundancy D-Bus interface
     */
    RedundancyInterface redundancyInterface;

    /**
     * @brief Contains the various provider helpers
     *
     * NOTE: Must be declared before handler so it's destroyed
     * after handler, since handler's destructor accesses providers.
     */
    std::unique_ptr<Providers> providers;

    /**
     * @brief The role handler class
     */
    std::unique_ptr<RoleHandler> handler;

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

    /**
     * @brief The reason the active/passive role was chosen.
     */
    role_determination::RoleReason roleReason{
        role_determination::RoleReason::unknown};

    /**
     * @brief The interval between heartbeats
     */
    std::chrono::milliseconds heartbeatInterval;
};

} // namespace rbmc
