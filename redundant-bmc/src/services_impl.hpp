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
     * @brief Gets the systemd unit state
     *
     * @param[in] - The unit/service name
     *
     * @return state - The systemd unit state
     */
    sdbusplus::async::task<std::string>
        getUnitState(const std::string& name) const;

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;
};

} // namespace rbmc
