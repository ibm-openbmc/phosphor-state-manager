/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/Sibling/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>

namespace rbmc
{

/**
 * @class Sibling
 *
 * Provides information about the Sibling BMC, getting it from
 * xyz.openbmc_project.State.BMC.Redundancy.Sibling.
 *
 * Will only provide data value when that Sibling interface is
 * present with the heartbeat property true, meaning the code
 * running on the sibling is alive.
 *
 * This is a pure virtual base class, so that these functions can
 * be mocked in test.
 */
class Sibling
{
  public:
    using Role =
        sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy::Role;
    using BMCState =
        sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState;
    using RedundancyEnabledCallback = std::function<void(bool)>;
    using BMCStateCallback = std::function<void(BMCState)>;
    using HeartbeatCallback = std::function<void(bool)>;
    using FailoversPausedCallback = std::function<void(bool)>;

    Sibling() = default;
    virtual ~Sibling() = default;
    Sibling(const Sibling&) = delete;
    Sibling& operator=(const Sibling&) = delete;
    Sibling(Sibling&&) = delete;
    Sibling& operator=(Sibling&&) = delete;

    /**
     * @brief The name of the unit/service.
     */
    static constexpr auto unitName =
        "xyz.openbmc_project.State.BMC.Redundancy.Sibling.service";

    /**
     * @brief Returns if the Sibling interface is on D-Bus
     */
    virtual bool getInterfacePresent() const = 0;

    /**
     * @brief Sets up the D-Bus matches
     *
     * @return - The task object
     */
    virtual sdbusplus::async::task<> init() = 0;

    /**
     * @brief Returns if the sibling heartbeat is active
     */
    virtual bool hasHeartbeat() const = 0;

    /**
     * @brief Waits up to 'timeout' for the sibling interface to
     *        be on D-Bus and have the heartbeat property active.
     *
     * @return - The task object
     */
    virtual sdbusplus::async::task<> waitForSiblingUp(
        const std::chrono::seconds& timeout) = 0;

    /**
     * @brief Waits for the sibling role to change, assuming that the
     *        sibling is alive and hasn't determined a role yet.
     */
    virtual sdbusplus::async::task<> waitForSiblingRole() = 0;

    /**
     * @brief Waits for up to 10 minutes for the sibling BMC to
     *        reach steady state - either Ready or Quiesced.
     */
    virtual sdbusplus::async::task<> waitForBMCSteadyState() const = 0;

    /**
     * @brief Returns the sibling BMC's position
     *
     * @return - The position or nullopt if not available
     */
    virtual std::optional<size_t> getPosition() const = 0;

    /**
     * @brief Returns the sibling BMC's state
     *
     * @return - The state or nullopt if not available
     */
    virtual std::optional<BMCState> getBMCState() const = 0;

    /**
     * @brief Returns the sibling BMC's role
     *
     * @return - The role, or nullopt if not available
     */
    virtual std::optional<Role> getRole() const = 0;

    /**
     * @brief Returns if the sibling has redundancy enabled.
     *
     * @return - If enabled, or nullopt if not available
     */
    virtual std::optional<bool> getRedundancyEnabled() const = 0;

    /**
     * @brief Returns the sibling BMC's provisioning status
     *
     * @return - The status, or nullopt if not available
     */
    virtual std::optional<bool> getProvisioned() const = 0;

    /**
     * @brief Returns the sibling BMC's FW version representation
     *
     * @return - The version, or nullopt if not available
     */
    virtual std::optional<std::string> getFWVersion() const = 0;

    /**
     * @brief Returns the sibling BMC's commsOK value
     *
     * @return - The value, or nullopt if not available
     */
    virtual std::optional<bool> getSiblingCommsOK() const = 0;

    /**
     * @brief Returns if the sibling has failovers paused
     *
     * @return - If paused, or nullopt if not available
     */
    virtual std::optional<bool> getFailoversPaused() const = 0;

    /**
     * @brief Returns if the sibling BMC is plugged in
     *
     * @return bool - if present
     */
    virtual bool isBMCPresent() = 0;

    /**
     * @brief Pause for the amount of time it would take for a heartbeat
     *        change to be noticed.
     */
    virtual sdbusplus::async::task<> pauseForHeartbeatChange() const = 0;

    /**
     * @brief Clears callbacks held based on role
     *
     * @param[in] role - The role to clear
     */
    void clearCallbacks(Role role)
    {
        redEnabledCBs.erase(role);
        bmcStateCBs.erase(role);
        heartbeatCBs.erase(role);
        foPausedCBs.erase(role);
    }

    /**
     * @brief Adds a callback function to invoke when the sibling's
     *        RedundancyEnabled property changes
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addRedundancyEnabledCallback(Role role,
                                      RedundancyEnabledCallback callback)
    {
        redEnabledCBs.emplace(role, std::move(callback));
    }

    /**
     * @brief Adds a callback function to invoke when the sibling's
     *        Heartbeat property changes
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addBMCStateCallback(Role role, BMCStateCallback callback)
    {
        bmcStateCBs.emplace(role, std::move(callback));
    }

    /**
     * @brief Adds a callback function to invoke when the sibling's
     *        Heartbeat property changes
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addHeartbeatCallback(Role role, HeartbeatCallback callback)
    {
        heartbeatCBs.emplace(role, std::move(callback));
    }

    /**
     * @brief Adds a callback function to invoke when the sibling's
     *        FailoversPaused property changes
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addFailoversPausedCallback(Role role, FailoversPausedCallback callback)
    {
        foPausedCBs.emplace(role, std::move(callback));
    }

  protected:
    /**
     * @brief Callbacks for RedundancyEnabled
     */
    std::map<Role, RedundancyEnabledCallback> redEnabledCBs;

    /**
     * @brief Callbacks for BMCState
     */
    std::map<Role, BMCStateCallback> bmcStateCBs;

    /**
     * @brief Callbacks for Heartbeat
     */
    std::map<Role, HeartbeatCallback> heartbeatCBs;

    /**
     * @brief Callbacks for FailoversPaused
     */
    std::map<Role, FailoversPausedCallback> foPausedCBs;
};
} // namespace rbmc
