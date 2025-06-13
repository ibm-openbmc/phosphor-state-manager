/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "sibling.hpp"

namespace rbmc
{

/**
 * @class SiblingImpl
 *
 * Implements the Sibling functionality.  Provides cached
 * access to the sibling data members, assuming the interface
 * is on D-Bus and the heartbeat is active.
 */
class SiblingImpl : public Sibling
{
  public:
    using PropertyVariant =
        std::variant<std::string, bool, Role, size_t, BMCState>;
    using PropertyMap = std::unordered_map<std::string, PropertyVariant>;
    using InterfaceMap = std::map<std::string, PropertyMap>;
    using ManagedObjects =
        std::map<sdbusplus::message::object_path, InterfaceMap>;

    ~SiblingImpl() override = default;
    SiblingImpl(const SiblingImpl&) = delete;
    SiblingImpl& operator=(const SiblingImpl&) = delete;
    SiblingImpl(SiblingImpl&&) = delete;
    SiblingImpl& operator=(SiblingImpl&&) = delete;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     */
    explicit SiblingImpl(sdbusplus::async::context& ctx);

    /**
     * @brief Returns if the Sibling interfaces are on D-Bus
     */
    bool getInterfacePresent() const override
    {
        return version.present && redundancy.present && bmcState.present &&
               heartbeat.present;
    }

    /**
     * @brief Returns if the sibling heartbeat is active
     */
    bool hasHeartbeat() const override
    {
        return heartbeat.active;
    }

    /**
     * @brief Sets up the D-Bus matches
     *
     * @return - The task object
     */
    sdbusplus::async::task<> init() override;

    /**
     * @brief Waits up to 6 minutes for the sibling interface to
     *        be on D-Bus and have the heartbeat property active.
     *
     * @return - The task object
     */
    sdbusplus::async::task<> waitForSiblingUp() override;

    /**
     * @brief Waits for the sibling role to change, assuming that the
     *        sibling is alive and hasn't determined a role yet.
     *
     * This is used to give the previously active BMC a head start
     * to be active again, which also avoids possible race conditions
     * when they both come up at the same time.
     */
    sdbusplus::async::task<> waitForSiblingRole() override;

    /**
     * @brief Returns the sibling BMC's state
     *
     * @return - The state or nullopt if not available
     */
    std::optional<BMCState> getBMCState() const override
    {
        if (getInterfacePresent() && hasHeartbeat())
        {
            return bmcState.state;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns the sibling BMC's role
     *
     * @return - The role, or nullopt if not available
     */
    std::optional<Role> getRole() const override
    {
        if (getInterfacePresent() && hasHeartbeat())
        {
            return redundancy.role;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling has redundancy enabled.
     *
     * @return - If enabled, or nullopt if not available
     */
    std::optional<bool> getRedundancyEnabled() const override
    {
        if (getInterfacePresent() && hasHeartbeat())
        {
            return redundancy.redundancyEnabled;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns the sibling BMC's provisioning status
     *
     * @return - The status, or nullopt if not available
     */
    std::optional<bool> getProvisioned() const override
    {
        if (getInterfacePresent() && hasHeartbeat())
        {
            // TBD which interface to use.
            return true;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns the sibling BMC's FW version representation
     *
     * @return - The version, or nullopt if not available
     */
    std::optional<std::string> getFWVersion() const override
    {
        if (getInterfacePresent() && hasHeartbeat())
        {
            return version.version;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling has failovers allowed.
     *
     * @return - If allowed, or nullopt if not available
     */
    std::optional<bool> getFailoversAllowed() const override
    {
        if (getInterfacePresent() && hasHeartbeat())
        {
            return redundancy.failoversAllowed;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling BMC is plugged in
     *
     * @return bool - if present
     */
    bool isBMCPresent() override;

    /**
     * @brief Waits for up to 10 minutes for the sibling BMC to
     *        reach steady state - either Ready or Quiesced.
     */
    sdbusplus::async::task<> waitForBMCSteadyState() const override;

    /**
     * @brief Pause for the amount of time it would take for a heartbeat
     *        change to be noticed.
     */
    sdbusplus::async::task<> pauseForHeartbeatChange() const override;

  private:
    /**
     * @brief Starts a Sibling InterfacesAdded watch
     */
    sdbusplus::async::task<> watchInterfaceAdded();

    /**
     * @brief Starts a Sibling InterfacesRemoved watch
     */
    sdbusplus::async::task<> watchInterfaceRemoved();

    /**
     * @brief Starts a Sibling NameOwnerChanged watch
     */
    sdbusplus::async::task<> watchNameOwnerChanged();

    /**
     * @brief Starts a PropertyChanged watch for all interfaces
     */
    sdbusplus::async::task<> watchPropertyChanged();

    /**
     * @brief Sets initial values for all properties
     */
    sdbusplus::async::task<> initProperties();

    /**
     * @brief Sets redundancy data members with whatever is in the property map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadRedundancyProps(const PropertyMap& propertyMap);

    /**
     * @brief Sets version data members with whatever is in the property map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadVersionProps(const PropertyMap& propertyMap);

    /**
     * @brief Sets state data members with whatever is in the property map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadStateProps(const PropertyMap& propertyMap);

    /**
     * @brief Sets heartbeat data members with whatever is in the property map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadHeartbeatProps(const PropertyMap& propertyMap);

    /**
     * @brief Sets data members with whatever is in the property map
     *
     * @param[in] interface - The interface name
     * @param[in] propertyMap - The property name -> value map
     */
    void loadFromPropertyMap(const std::string& interface,
                             const PropertyMap& propertyMap);

    /**
     * @brief Sets all interfaces to not present
     */
    void setInterfacesNotPresent()
    {
        redundancy.present = false;
        bmcState.present = false;
        version.present = false;
        heartbeat.present = false;
        heartbeat.active = false;
    }

    /**
     * @brief Gets the sibling D-Bus service name from the mapper
     *
     * @return std::string - The service name
     */
    sdbusplus::async::task<std::string> getServiceName();

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The sibling's D-Bus service name.
     */
    std::string serviceName;

    /**
     * @brief If init() has been called
     */
    bool initialized = false;

    struct Version
    {
        bool present = false;
        std::string version;
    };

    /**
     * @brief Version presence and value
     */
    Version version;

    struct Redundancy
    {
        bool present = false;
        Role role = Role::Unknown;
        bool redundancyEnabled = false;
        bool failoversAllowed = false;
    };

    /**
     * @brief Redundancy presence and values
     */
    Redundancy redundancy;

    struct State
    {
        bool present = false;
        BMCState state = BMCState::NotReady;
    };

    /**
     * @brief State presence and value
     */
    State bmcState;

    struct Heartbeat
    {
        bool present = false;
        bool active = false;
    };

    /**
     * @brief Heartbeat presence and value
     */
    Heartbeat heartbeat;

    /**
     * @brief The D-Bus object path for the sibling.
     */
    std::string objectPath;
};

} // namespace rbmc
