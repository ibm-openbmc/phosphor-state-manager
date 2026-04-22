/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "sibling.hpp"

#include <sdbusplus/async/barrier.hpp>

#include <vector>

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
        std::variant<std::string, bool, Role, size_t, BMCState,
                     std::vector<ReasonForNoRedundancy>>;
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
     * @brief Returns if the sibling BMC has a good heartbeat
     *        and there is valid data on D-Bus for it.
     *
     * The sibling daemon only puts the interfaces on D-Bus when
     * the heartbeat is active.
     */
    bool alive() const override
    {
        return version.present && redundancy.present && bmcState.present &&
               availability.present;
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
        if (alive())
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
        if (alive())
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
        if (alive())
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
        if (alive())
        {
            return provisioning.provisioned;
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
        if (alive())
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
        if (alive())
        {
            return redundancy.failoversAllowed;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling has a failover in progress
     *
     * @return - If in progress, or nullopt if not available
     */
    std::optional<bool> getFailoverInProgress() const override
    {
        if (alive())
        {
            return redundancy.failoverInProgress;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling has a failover imminent
     *
     * @return - If imminent, or nullopt if not available
     */
    std::optional<bool> getFailoverImminent() const override
    {
        if (alive())
        {
            return redundancy.failoverImminent;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling a reason redundancy can't be enabled
     *
     * @return - If there is a reason, or nullopt if not available
     */
    std::optional<bool> getHasReasonForNoRedundancy() const override
    {
        if (alive())
        {
            return redundancy.hasReasonForNoRedundancy;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling BMC is plugged in
     *
     * @return bool - if present
     */
    bool isBMCPresent() override
    {
        return availability.available;
    }

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

    /**
     * @brief Pause to allow time for data from the sibling BMC to propagate.
     */
    sdbusplus::async::task<> pauseForDataPropagation() const override;

    /**
     * @brief Returns the D-Bus service name for the sibling service.
     *
     * Returns an empty string if it isn't on D-Bus.
     */
    const std::string& getServiceName() const override
    {
        return serviceName;
    }

  private:
    /**
     * @brief Starts a Sibling InterfacesAdded watch
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchInterfaceAdded(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Starts a Sibling InterfacesRemoved watch
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchInterfaceRemoved(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Starts a Sibling NameOwnerChanged watch
     */
    sdbusplus::async::task<> watchNameOwnerChanged();

    /**
     * @brief Starts a PropertyChanged watch for all interfaces
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchPropertyChanged(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

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
     * @brief Sets Availability data members with whatever is in the property
     * map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadAvailabilityProps(const PropertyMap& propertyMap);

    /**
     * @brief Sets Provisioning data members with whatever is in the property
     * map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadProvisioningProps(const PropertyMap& propertyMap);

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
        availability.present = false;
        provisioning.present = false;
    }

    /**
     * @brief Looks up the sibling D-Bus service name from the mapper
     *
     * Will do retries for up to 10 seconds.
     *
     * @return std::string - The service name or empty string if not found.
     */
    sdbusplus::async::task<std::string> lookupServiceName() const;

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
        bool failoverInProgress = false;
        bool failoverImminent = false;
        bool hasReasonForNoRedundancy = false;
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

    struct Availability
    {
        bool present = false;
        bool available = false;
    };

    /**
     * @brief Availability presence and value
     */
    Availability availability;

    struct Provisioning
    {
        bool present = false;
        bool provisioned = false;
    };

    /**
     * @brief Provisioning presence and value
     */
    Provisioning provisioning;

    /**
     * @brief The D-Bus object path for the sibling.
     */
    std::string objectPath;
};

} // namespace rbmc
