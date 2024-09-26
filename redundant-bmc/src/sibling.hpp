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

    Sibling() = default;
    virtual ~Sibling() = default;
    Sibling(const Sibling&) = delete;
    Sibling& operator=(const Sibling&) = delete;
    Sibling(Sibling&&) = delete;
    Sibling& operator=(Sibling&&) = delete;

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
    virtual sdbusplus::async::task<>
        waitForSiblingUp(const std::chrono::seconds& timeout) = 0;

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
     * @brief Returns if the sibling BMC is plugged in
     *
     * @return bool - if present
     */
    virtual bool isBMCPresent() = 0;
};
} // namespace rbmc
