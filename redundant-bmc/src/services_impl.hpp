/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "services.hpp"
#include "wait_tracker.hpp"

#include <sdbusplus/async/barrier.hpp>
#include <xyz/openbmc_project/Provisioning/Provisioning/common.hpp>
#include <xyz/openbmc_project/State/Boot/Progress/common.hpp>
#include <xyz/openbmc_project/State/Host/common.hpp>

#include <optional>

namespace rbmc
{

const std::filesystem::path persistentDataPath =
    "/var/lib/phosphor-state-manager/redundant-bmc";

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

    using PairingCommon =
        sdbusplus::common::xyz::openbmc_project::provisioning::Provisioning;
    using PairingPropMap =
        std::unordered_map<std::string, PairingCommon::PropertiesVariant>;
    using PairingInterfaceMap = std::map<std::string, PairingPropMap>;

    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] waitTracker - The wait tracker
     */
    ServicesImpl(sdbusplus::async::context& ctx, WaitTracker& waitTracker) :
        ctx(ctx), waitTracker(waitTracker)
    {}

    /**
     * @brief Sets up watches on the host state
     */
    sdbusplus::async::task<> init() override;

    /**
     * @brief Returns this BMC's position.
     *
     * @return - The position if it can be obtained
     */
    std::optional<size_t> getBMCPosition() const override;

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
    sdbusplus::async::task<> startUnit(
        const std::string& unitName,
        std::chrono::seconds timeout) const override;

    /**
     * @brief Gets the systemd unit state
     *
     * @param[in] - The unit/service name
     *
     * @return state - The systemd unit state
     */
    sdbusplus::async::task<std::string> getUnitState(
        const std::string& name) const override;

    /**
     * @brief If this BMC has been paired
     *
     * @return bool - If paired or not.
     */
    bool getPaired() const override;

    /**
     * @brief Returns an 8 character hash of the FW version
     *
     * It's read out of the VERSION_ID field in /etc/os-release.
     *
     * @return - The version hash
     */
    std::string getFWVersion() const override;

    /**
     * @brief Returns the system state
     *
     * @return The system state
     */
    SystemState getSystemState() const override;

    /**
     * @brief Returns if the peer is connected
     *
     * @return true if PeerConnected status is Connected, false otherwise
     */
    bool getPeerConnected() const override
    {
        return peerConnected;
    }

    /**
     * @brief Waits for the PeerConnected property to reach Connected
     *
     * @param[in] shouldAbort - Optional predicate to check if wait should abort
     *                          early. If it returns true, the wait will stop.
     */
    sdbusplus::async::task<> waitForPeerConnection(
        AbortPredicate shouldAbort = nullptr) override;

    /**
     * @brief Reads the BMC state
     *
     * @return The BMC state
     */
    sdbusplus::async::task<
        sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState>
        getBMCState() const override;

    /**
     * @brief Execute the 'failover imminent' delay to the other BMC
     *        has a warning that a failover is imminent and that it will
     *        be reset.
     */
    sdbusplus::async::task<> doFailoverImminentDelay() const override;

    /**
     * @brief Flush the journal to the filesystem with
     *        "journalctl --sync"
     */
    sdbusplus::async::task<> flushJournal() const override;

    /**
     * @brief Returns the persistent data directory
     */
    std::filesystem::path getPersistentDataPath() const override
    {
        return persistentDataPath;
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
    sdbusplus::async::task<bool> checkSystemInventoryStatus() override;

    /**
     * @brief Acquires hardware access to the rest of the
     * system from this BMC if the hardware requires it.
     */
    sdbusplus::async::task<> acquireFullHardwareAccess() override;

    /**
     * @brief Creates an event log with the specified fields
     *
     * @param[in] error - The error enum to use
     * @param[in] severity - The log severity
     * @param[in] data - AdditionalData property value
     */
    sdbusplus::async::task<> logError(
        std::string error, errors::Level severity,
        errors::AdditionalData data) const override;

    /**
     * @brief Creates the redundancy determined marker file
     *
     * Creates /run/openbmc/bmc_redundancy_determined to indicate
     * that redundancy determination is complete.
     */
    void setRedundancyDetermined() override;

    /**
     * @brief Waits for the Paired property to become true
     *
     * Waits up to 30 seconds.
     */
    sdbusplus::async::task<> waitForSelfPairing() override;

  private:
    /**
     * @brief Returns the D-Bus object path for the unit in the
     *        systemd namespace.
     *
     * @param[in] unitName - The unit name, like obmc-bmc-active.target
     *
     * @return object_path - The systemd D-Bus object path
     */
    sdbusplus::async::task<sdbusplus::message::object_path> getUnitPath(
        const std::string& unitName) const;

    /**
     * @brief Lists all active systemd jobs and logs them to the journal
     *
     * This is useful for debugging when a unit fails to start within
     * the expected timeout period.
     */
    sdbusplus::async::task<> listAndLogSystemdJobs() const;

    /**
     * @brief Starts the InterfacesAdded watch for the host state
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchHostInterfacesAdded(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Starts the PropertiesChanged watch for the host state
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchHostStatePropertiesChanged(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Reads the CurrentHostState property
     */
    sdbusplus::async::task<> readHostState();

    /**
     * @brief Reads the BootProgress property
     */
    sdbusplus::async::task<> readBootProgress();

    /**
     * @brief Starts the PropertiesChanged watch for the BootProgress property
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchBootProgressPropertiesChanged(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Starts the InterfacesAdded watch for the Pairing interface
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchPairingInterfacesAdded(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Starts the PropertiesChanged watch for the Pairing interface
     *
     * @param[in] barrier - Initialization barrier
     */
    sdbusplus::async::task<> watchPairingPropertiesChanged(
        std::shared_ptr<sdbusplus::async::barrier> barrier);

    /**
     * @brief Reads the Pairing interface properties
     */
    sdbusplus::async::task<> readPairingProperties();

    void loadPairingProps(const PairingPropMap& propertyMap);

    /**
     * @brief Called when either the host state or boot progress property
     *        changes value to calculate the system state.
     */
    void updateSystemState();

    /**
     * @brief Waits up to 3 minutes for the Item.System interface
     *        to show up and then saves the path in systemInvPath.
     */
    sdbusplus::async::task<> waitForSystemInventoryPath();

    /**
     * @brief The async context object
     */
    sdbusplus::async::context& ctx;

    /**
     * @brief The wait tracker
     */
    WaitTracker& waitTracker;

    /**
     * @brief The host state value
     */
    std::optional<
        sdbusplus::common::xyz::openbmc_project::state::Host::HostState>
        hostState;

    /**
     * @brief The boot progress value
     */
    std::optional<sdbusplus::common::xyz::openbmc_project::state::boot::
                      Progress::ProgressStages>
        bootProgress;

    /**
     * @brief The current system state value
     */
    std::optional<SystemState> systemState;

    /**
     * @brief Tracks peer connection state
     */
    bool peerConnected{false};

    /**
     * @brief The paired status value
     */
    bool paired{false};

    /**
     * @brief D-Bus path for the Item.System object
     */
    std::string systemInvPath;
};

} // namespace rbmc
