/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "sibling.hpp"

namespace rbmc
{

namespace redundancy_ns =
    sdbusplus::common::xyz::openbmc_project::state::bmc::redundancy;

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
    using PropertyMap =
        std::unordered_map<std::string,
                           redundancy_ns::Sibling::PropertiesVariant>;
    using InterfaceMap = std::map<std::string, PropertyMap>;
    using SiblingNP = redundancy_ns::Sibling::namespace_path;

    SiblingImpl() = delete;
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
    explicit SiblingImpl(sdbusplus::async::context& ctx) :
        ctx(ctx),
        objectPath(std::string{SiblingNP::value} + '/' + SiblingNP::bmc)
    {}

    /**
     * @brief Returns if the Sibling interface is on D-Bus
     */
    bool getInterfacePresent() const override
    {
        return interfacePresent;
    }

    /**
     * @brief Returns if the sibling heartbeat is active
     */
    bool hasHeartbeat() const override
    {
        return heartbeat;
    }

    /**
     * @brief Sets up the D-Bus matches
     *
     * @return - The task object
     */
    sdbusplus::async::task<> init() override;

    /**
     * @brief Waits up to 'timeout' for the sibling interface to
     *        be on D-Bus and have the heartbeat property active.
     *
     * @return - The task object
     */
    sdbusplus::async::task<>
        waitForSiblingUp(const std::chrono::seconds& timeout) override;

    /**
     * @brief Returns the sibling BMC's position
     *
     * @return - The position or nullopt if not available
     */
    std::optional<size_t> getPosition() const override
    {
        if (interfacePresent && heartbeat)
        {
            return bmcPosition;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns the sibling BMC's state
     *
     * @return - The state or nullopt if not available
     */
    std::optional<BMCState> getBMCState() const override
    {
        if (interfacePresent && heartbeat)
        {
            return bmcState;
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
        if (interfacePresent && heartbeat)
        {
            return role;
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
        if (interfacePresent && heartbeat)
        {
            return redEnabled;
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
        if (interfacePresent && heartbeat)
        {
            return provisioned;
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
        if (interfacePresent && heartbeat)
        {
            return fwVersion;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns the sibling BMC's commsOK value
     *
     * @return - The value, or nullopt if not available
     */
    std::optional<bool> getSiblingCommsOK() const override
    {
        if (interfacePresent && heartbeat)
        {
            return commsOK;
        }

        return std::nullopt;
    }

    /**
     * @brief Returns if the sibling BMC is plugged in
     *
     * @return bool - if present
     */
    bool isBMCPresent() override;

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
     * @brief Starts a Sibling PropertyChanged watch
     */
    sdbusplus::async::task<> watchPropertyChanged();

    /**
     * @brief Sets data members with whatever is in the property map
     *
     * @param[in] propertyMap - The property name -> value map
     */
    void loadFromPropertyMap(const PropertyMap& propertyMap);

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
     * @brief If the Sibling interface is on D-Bus
     */
    bool interfacePresent = false;

    /**
     * @brief If init() has been called
     */
    bool initialized = false;

    /**
     * @brief The sibling's BMC position
     */
    size_t bmcPosition = 0;

    /**
     * @brief The sibling's FW version string
     */
    std::string fwVersion;

    /**
     * @brief The sibling's provisioning status
     */
    bool provisioned = false;

    /**
     * @brief The sibling's redundancy enabled field
     */
    bool redEnabled = false;

    /**
     * @brief If sibling failovers are paused
     */
    bool failoversPaused = false;

    /**
     * @brief The sibling's BMC state
     */
    BMCState bmcState{};

    /**
     * @brief The sibling's role
     */
    Role role = Role::Unknown;

    /**
     * @brief If the sibling can talk to this BMC
     */
    bool commsOK = false;

    /**
     * @brief If the sibling heartbeat is active.
     */
    bool heartbeat = false;

    /**
     * @brief The D-Bus object path for the sibling.
     */
    std::string objectPath;
};

} // namespace rbmc
