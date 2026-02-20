/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>

#include <filesystem>

namespace rbmc
{

enum class SystemState
{
    off,
    booting,
    runtime,
    other
};

/**
 * @class Services
 *
 * This is the base class interface for dealing with system
 * information, so that the details on how to obtain the data
 * are abtracted away from the business logic.
 *
 * In addition to a derived class the implements the functionality,
 * a mock version of the class can be used in unit test.
 *
 */
class Services
{
  public:
    Services() = default;
    virtual ~Services() = default;
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;
    Services(Services&&) = delete;
    Services& operator=(Services&&) = delete;

    using SystemStateCallback = std::function<void(SystemState)>;

    /**
     * @brief Sets up the D-Bus matches
     *
     * @return - The task object
     */
    virtual sdbusplus::async::task<> init() = 0;

    /**
     * @brief Returns this BMC's position.
     *
     * @return - The position if it can be obtained
     */
    virtual sdbusplus::async::task<std::optional<size_t>> getBMCPosition()
        const = 0;

    /**
     * @brief Starts a systemd unit
     *
     * Will return after unit becomes active, otherwise will
     * throw if:
     *   1. Unit goes to failed
     *   2. The timeout is reached.
     *
     * @param[in] unitName - The unit name
     * @param[in] timeout - Timeout value.
     */
    virtual sdbusplus::async::task<> startUnit(
        const std::string& unitName, std::chrono::seconds timeout) const = 0;

    /**
     * @brief Gets the systemd unit state
     *
     * @param[in] - The unit/service name
     *
     * @return state - The systemd unit state
     */
    virtual sdbusplus::async::task<std::string> getUnitState(
        const std::string& unitName) const = 0;

    /**
     * @brief If this BMC has been provisioned
     *
     * @return bool - If provisioned or not.
     */
    virtual bool getProvisioned() const = 0;

    /**
     * @brief Returns an 8 character hash of the FW version
     *
     * It's read out of the VERSION_ID field in /etc/os-release.
     *
     * @return - The version hash
     */
    virtual std::string getFWVersion() const = 0;

    /**
     * @brief Reads the BMC state
     *
     * @return The BMC state
     */
    virtual sdbusplus::async::task<
        sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState>
        getBMCState() const = 0;

    /**
     * @brief Returns the system state
     *
     * @return The system state
     */
    virtual SystemState getSystemState() const = 0;

    /**
     * @brief Execute the 'failover imminent' delay to the other BMC
     *        has a warning that a failover is imminent and that it will
     *        be reset.
     */
    virtual sdbusplus::async::task<> doFailoverImminentDelay() const = 0;

    /**
     * @brief Flush the journal to the filesystem.
     */
    virtual sdbusplus::async::task<> flushJournal() const = 0;

    /**
     * @brief Returns the persistent data directory
     */
    virtual std::filesystem::path getPersistentDataPath() const = 0;

    /**
     * @brief Returns the string name for the system state enum
     *
     * @param[in] state - The state enum
     *
     * @return The string name
     */
    static inline std::string getSystemStateName(SystemState state)
    {
        switch (state)
        {
            case SystemState::off:
                return "Off";
            case SystemState::booting:
                return "Booting";
            case SystemState::runtime:
                return "Runtime";
            case SystemState::other:
                return "Other";
        }
        return std::string{"Unknown"};
    }

    /**
     * @brief Add a function that gets called when the system state changes.
     *
     * @param[in] callback - The function to call
     */
    void addSystemStateCallback(SystemStateCallback&& callback)
    {
        systemStateCBs.push_back(std::move(callback));
    }

    /**
     * @brief Clears all system state change callbacks.
     */
    void clearSystemStateCallbacks()
    {
        systemStateCBs.clear();
    }

    /**
     * @brief On the system inventory object, check that its Progress
     *        Status property is 'Completed'.
     *
     * It will wait if the property isn't Completed or Failed/Aborted.
     *
     * If the interface doesn't exist on that path, just return true as
     * it's assumed it isn't implemented.
     *
     * @return true if the status is complete, false else
     */
    virtual sdbusplus::async::task<bool> checkSystemInventoryStatus() = 0;

    /**
     * @brief Acquires hardware access to the rest of the
     * system from this BMC if the hardware requires it.
     */
    virtual sdbusplus::async::task<> acquireFullHardwareAccess() = 0;

  protected:
    /**
     * @brief The functions to call when the system state changes
     */
    std::vector<SystemStateCallback> systemStateCBs;
};

} // namespace rbmc
