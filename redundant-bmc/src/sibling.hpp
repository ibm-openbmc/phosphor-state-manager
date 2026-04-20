/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>

namespace rbmc
{

/**
 * @class Sibling
 *
 * Provides information about the Sibling BMC.
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
    using ReasonForNoRedundancy = sdbusplus::common::xyz::openbmc_project::
        state::bmc::Redundancy::ReasonForNoRedundancy;
    using RedundancyEnabledCallback = std::function<void(bool)>;
    using BMCStateCallback = std::function<void(BMCState)>;
    using HealthCallback = std::function<void(bool)>;
    using FailoversAllowedCallback = std::function<void(bool)>;
    using FailoverImminentCallback = std::function<void(bool)>;

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
     * @brief Sets up the D-Bus matches
     *
     * @return - The task object
     */
    virtual sdbusplus::async::task<> init() = 0;

    /**
     * @brief Returns if the sibling BMC has a good heartbeat
     *        and there is valid data on D-Bus for it.
     */
    virtual bool alive() const = 0;

    /**
     * @brief Waits up to 6 minutes for the sibling interface to
     *        be on D-Bus and have the heartbeat property active.
     *
     * @return - The task object
     */
    virtual sdbusplus::async::task<> waitForSiblingUp() = 0;

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
     * @brief Returns if the sibling has failovers allowed
     *
     * @return - If allowed, or nullopt if not available
     */
    virtual std::optional<bool> getFailoversAllowed() const = 0;

    /**
     * @brief Returns if the sibling has a failover in progress
     *
     * @return - If in progress, or nullopt if not available
     */
    virtual std::optional<bool> getFailoverInProgress() const = 0;

    /**
     * @brief Returns if the sibling has a failover imminent
     *
     * @return - If imminent, or nullopt if not available
     */
    virtual std::optional<bool> getFailoverImminent() const = 0;

    /**
     * @brief Returns if the sibling a reason redundancy can't be enabled
     *
     * @return - If there is a reason, or nullopt if not available
     */
    virtual std::optional<bool> getHasReasonForNoRedundancy() const = 0;

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
     * @brief Pause to allow time for data from the sibling BMC to propagate.
     */
    virtual sdbusplus::async::task<> pauseForDataPropagation() const = 0;

    /**
     * @brief Returns the D-Bus service name for the sibling service.
     *
     * Returns an empty string if it isn't on D-Bus.
     */
    virtual const std::string& getServiceName() const = 0;

    /**
     * @brief Clears callbacks held based on role
     *
     * @param[in] role - The role to clear
     */
    void clearCallbacks(Role role)
    {
        redEnabledCBs.erase(role);
        bmcStateCBs.erase(role);
        healthCBs.erase(role);
        foAllowedCBs.erase(role);
        foImminentCBs.erase(role);
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
     *        health changes, i.e. its D-Bus interfaces either show
     *        up or disappear.
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addHealthCallback(Role role, HealthCallback callback)
    {
        healthCBs.emplace(role, std::move(callback));
    }

    /**
     * @brief Adds a callback function to invoke when the sibling's
     *        FailoversAllowed property changes
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addFailoversAllowedCallback(Role role,
                                     FailoversAllowedCallback callback)
    {
        foAllowedCBs.emplace(role, std::move(callback));
    }

    /**
     * @brief Adds a callback function to invoke when the sibling's
     *        FailoverImminent property changes
     *
     * @param[in] role - The role to register with
     * @param[in] callback - The callback function
     */
    void addFailoverImminentCallback(Role role,
                                     FailoverImminentCallback callback)
    {
        foImminentCBs.emplace(role, std::move(callback));
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
     * @brief Callbacks for when the health changes
     */
    std::map<Role, HealthCallback> healthCBs;

    /**
     * @brief Callbacks for FailoversAllowed
     */
    std::map<Role, FailoversAllowedCallback> foAllowedCBs;

    /**
     * @brief Callbacks for FailoverImminent
     */
    std::map<Role, FailoverImminentCallback> foImminentCBs;
};
} // namespace rbmc
