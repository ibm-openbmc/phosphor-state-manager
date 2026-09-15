// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "config_data.hpp"
#include "services.hpp"

#include <sdbusplus/async.hpp>
#include <sdbusplus/async/barrier.hpp>

namespace rbmc
{

/** @class SiblingChassisWatcher
 *
 * Watches the passive (sibling) BMC's parent chassis Available property on
 * D-Bus and provides a method to read the current value.
 *
 * When the property changes, the registered SiblingChassisAvailCallback
 * callbacks on the Services object are called.
 *
 * This assumes that the inventory is primed with the chassis objects for both
 * BMCs even if both aren't physically present by the time the watches are
 * started, so only PropertiesChanged signals need to be watched. If that
 * changes, an InterfacesAdded watch could be added as well.
 */
class SiblingChassisWatcher
{
  public:
    ~SiblingChassisWatcher() = default;
    SiblingChassisWatcher(const SiblingChassisWatcher&) = delete;
    SiblingChassisWatcher& operator=(const SiblingChassisWatcher&) = delete;
    SiblingChassisWatcher(SiblingChassisWatcher&&) = delete;
    SiblingChassisWatcher& operator=(SiblingChassisWatcher&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx     - The async context object
     * @param[in] config  - The redundant BMC configuration
     * @param[in] services - Services reference used to call callbacks and
     *                       obtain the local BMC position
     */
    SiblingChassisWatcher(sdbusplus::async::context& ctx,
                          const RedundantBMCConfig& config,
                          Services& services) :
        ctx(ctx), config(config), services(services)
    {}

    /**
     * @brief Initializes the chassis Available watch.
     *
     * Locates the sibling BMC's parent chassis on D-Bus and starts a
     * PropertiesChanged watch on its Availability interface.  Sets the
     * initial available() value from the current property state.
     *
     * If check_passive_bmc_chassis_available is false in the config the
     * cached value is set to true and the method returns immediately.
     */
    sdbusplus::async::task<> init();

    /**
     * @brief Returns the current sibling chassis Available value.
     *
     * @return true if the sibling chassis is available, false otherwise.
     */
    bool available() const
    {
        return chassisAvailable;
    }

  private:
    /**
     * @brief Finds the sibling BMC's parent chassis object on D-Bus.
     *
     * @return Optional (service, path) pair, or nullopt if not found.
     */
    sdbusplus::async::task<std::optional<std::pair<std::string, std::string>>>
        findSiblingChassis();

    /**
     * @brief Reads the initial Available value and starts the
     *        properties changed watch.
     *
     * @param[in] service - D-Bus service owning the chassis object
     * @param[in] path    - D-Bus object path of the chassis object
     */
    sdbusplus::async::task<> startChassisAvailableWatch(
        const std::string& service, const std::string& path);

    /**
     * @brief Watches for PropertiesChanged signals and calls callbacks
     *        on changes.
     *
     * @param[in] path    - D-Bus object path of the chassis object
     * @param[in] barrier - Initialization barrier shared with the caller
     */
    sdbusplus::async::task<> watchChassisAvailablePropertiesChanged(
        std::string path, std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The redundant BMC configuration
     */
    const RedundantBMCConfig& config;

    /**
     * @brief Services reference. Used for BMC position and callbacks
     */
    Services& services;

    /**
     * @brief Current sibling chassis Available value
     */
    bool chassisAvailable{true};
};

} // namespace rbmc
