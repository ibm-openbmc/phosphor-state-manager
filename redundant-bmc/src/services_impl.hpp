/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "services.hpp"

namespace rbmc
{

/**
 * @class ServicesImpl
 *
 * Implements the Services functions to interact
 * with the system.
 *
 */
class ServicesImpl : public Services
{
  public:
    ServicesImpl() = delete;
    ~ServicesImpl() override = default;
    ServicesImpl(const ServicesImpl&) = delete;
    ServicesImpl& operator=(const ServicesImpl&) = delete;
    ServicesImpl(ServicesImpl&&) = delete;
    ServicesImpl& operator=(ServicesImpl&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     */
    explicit ServicesImpl(sdbusplus::async::context& ctx) : ctx(ctx) {}

    /**
     * @brief Sets up watches on the host state
     */
    sdbusplus::async::task<> init() override;

    /**
     * @brief Returns this BMC's position.
     *
     * @return - The position
     */
    size_t getBMCPosition() const override;

    /**
     * @brief Starts a systemd unit
     *
     * Waits for it to be active or failed before returning.
     *
     * @param[in] unitName - The unit name
     */
    sdbusplus::async::task<>
        startUnit(const std::string& unitName) const override;

    /**
     * @brief Gets the systemd unit state
     *
     * @param[in] - The unit/service name
     *
     * @return state - The systemd unit state
     */
    sdbusplus::async::task<std::string>
        getUnitState(const std::string& name) const override;

    /**
     * @brief If this BMC has been provisioned
     *
     * @return bool - If provisioned or not.
     */
    bool getProvisioned() const override;

    /**
     * @brief Returns an 8 character hash of the FW version
     *
     * It's read out of the VERSION_ID field in /etc/os-release.
     *
     * @return - The version hash
     */
    std::string getFWVersion() const override;

    /**
     * @brief Says if main power is on.
     *
     * @return If power is on
     */
    bool isPoweredOn() const override;

  private:
    /**
     * @brief Returns the D-Bus object path for the unit in the
     *        systemd namespace.
     *
     * @param[in] unitName - The unit name, like obmc-bmc-active.target
     *
     * @return object_path - The systemd D-Bus object path
     */
    sdbusplus::async::task<sdbusplus::message::object_path>
        getUnitPath(const std::string& unitName) const;

    /**
     * @brief Starts the InterfacesAdded watch for the host state
     */
    sdbusplus::async::task<> watchHostInterfacesAdded();

    /**
     * @brief Starts the PropertiesChanged watch for the host state
     */
    sdbusplus::async::task<> watchHostPropertiesChanged();

    /**
     * @brief Reads the CurrentHostState property
     */
    sdbusplus::async::task<> readHostState();

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief If the host is powered on
     *
     * Only valid after the host state service has been started.
     * Will throw if called before then.
     */
    std::optional<bool> poweredOn;
};

} // namespace rbmc
