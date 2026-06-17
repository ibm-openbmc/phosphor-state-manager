/* SPDX-License-Identifier: Apache-2.0 */
#include "services_impl.hpp"

#include "system_state.hpp"

#include <openssl/evp.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/Progress/client.hpp>
#include <xyz/openbmc_project/Control/SideBandBus/client.hpp>
#include <xyz/openbmc_project/Inventory/Decorator/Position/client.hpp>
#include <xyz/openbmc_project/Inventory/Item/System/common.hpp>
#include <xyz/openbmc_project/Logging/Create/client.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/Provisioning/Provisioning/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>
#include <xyz/openbmc_project/State/Boot/Progress/client.hpp>
#include <xyz/openbmc_project/State/Host/client.hpp>

#include <format>
#include <fstream>

namespace rbmc
{

using HostState = sdbusplus::client::xyz::openbmc_project::state::Host<>;
using ObjectMapper = sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;
using BootProgress =
    sdbusplus::client::xyz::openbmc_project::state::boot::Progress<>;
using SystemInv =
    sdbusplus::common::xyz::openbmc_project::inventory::item::System;
using InvProgress = sdbusplus::client::xyz::openbmc_project::common::Progress<>;
using SidebandBus =
    sdbusplus::client::xyz::openbmc_project::control::SideBandBus<>;
using Pairing =
    sdbusplus::client::xyz::openbmc_project::provisioning::Provisioning<>;
using PeerConnectionStatus = sdbusplus::common::xyz::openbmc_project::
    provisioning::Provisioning::PeerConnectionStatus;

using HostProperties =
    std::variant<std::string, HostState::HostState, HostState::RestartCause,
                 HostState::Transition, std::set<HostState::Transition>,
                 BootProgress::ProgressStages>;
using HostPropMap = std::unordered_map<std::string, HostProperties>;
using HostInterfaceMap = std::map<std::string, HostPropMap>;

namespace rules = sdbusplus::bus::match::rules;

namespace object_path
{
constexpr auto systemd = "/org/freedesktop/systemd1";

// host0 represents the overall host state
const std::string hostState = std::string{HostState::namespace_path::value} +
                              '/' + HostState::namespace_path::host + '0';
} // namespace object_path

namespace interface
{
constexpr auto systemdMgr = "org.freedesktop.systemd1.Manager";
constexpr auto systemdUnit = "org.freedesktop.systemd1.Unit";
} // namespace interface

namespace service
{
constexpr auto systemd = "org.freedesktop.systemd1";
} // namespace service

namespace util
{

// NOLINTNEXTLINE
sdbusplus::async::task<std::string> getService(sdbusplus::async::context& ctx,
                                               const std::string& path,
                                               const std::string& interface)
{
    auto mapper = ObjectMapper(ctx)
                      .service(ObjectMapper::default_service)
                      .path(ObjectMapper::instance_path);

    std::vector<std::string> iface{interface};
    auto object = co_await mapper.get_object(path, iface);
    co_return object.begin()->first;
}

sdbusplus::async::task<std::string> findSystemInventoryPath(
    sdbusplus::async::context& ctx)
{
    auto mapper = ObjectMapper(ctx)
                      .service(ObjectMapper::default_service)
                      .path(ObjectMapper::instance_path);

    std::vector<std::string> systemIface{SystemInv::interface};

    auto objects = co_await mapper.get_sub_tree(
        "/xyz/openbmc_project/inventory", 0, systemIface);

    if (objects.empty())
    {
        throw std::runtime_error("No system inventory object found");
    }

    // Until there is a reason to expect more, check
    // that there is just one System interface.
    if (objects.size() != 1)
    {
        throw std::invalid_argument(std::format(
            "Wrong number of system inventory objects: {}", objects.size()));
    }

    co_return objects.begin()->first;
}

/**
 * @brief Run a shell command asynchronously
 *
 * Use the pipe()/fork() paradigm to fork off a child process to run
 * the command. The child will write the command RC to its FD obtained
 * from pipe() and exit. Meanwhile the parent will use async::fdio to
 * asynchronously wait for that rc to show up in its read FD it had
 * obtained from pipe().
 */
// NOLINTNEXTLINE
sdbusplus::async::task<int> runAsyncCmd(sdbusplus::async::context& ctx,
                                        const std::string& cmd)
{
    int pipeFDs[2];

    // Open the read and write pipes
    if (pipe(pipeFDs) == -1)
    {
        auto e = errno;
        lg2::error("runAsyncCmd: pipe() failed with errno: {ERRNO}", "ERRNO",
                   e);
        co_return -1;
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        auto e = errno;
        close(pipeFDs[0]);
        close(pipeFDs[1]);
        lg2::error("runAsyncCmd: fork failed with errno {ERRNO}", "ERRNO", e);
        co_return -1;
    }
    else if (pid == 0)
    {
        // Child

        // Close the read pipe
        close(pipeFDs[0]);

        // NOLINTNEXTLINE(cert-env33-c)
        int rc = std::system(cmd.c_str());

        int exitCode = (rc == -1) ? -1 : (WIFEXITED(rc) ? WEXITSTATUS(rc) : -1);

        // Write the exit code to the write pipe
        ssize_t s = write(pipeFDs[1], &exitCode, sizeof(exitCode));

        _exit((s == sizeof(rc)) ? 0 : 1);
    }

    // In the parent here.

    // close the write pipe
    close(pipeFDs[1]);

    // Async wait for the child to write the command's rc to the read pipe
    sdbusplus::async::fdio fdio(ctx, pipeFDs[0]);
    co_await fdio.next();

    int cmdRC = -1;
    ssize_t bytesRead = read(pipeFDs[0], &cmdRC, sizeof(cmdRC));
    close(pipeFDs[0]);

    if (bytesRead != sizeof(cmdRC))
    {
        lg2::error("runAsyncCmd: Failed to read return code from command {CMD}",
                   "CMD", cmd);
        co_return -1;
    }

    // Wait for child to exit
    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
        lg2::error("runAsyncCmd: waitpid failed for command {CMD}", "CMD", cmd);
        co_return -1;
    }

    co_return cmdRC;
}

} // namespace util

sdbusplus::async::task<> ServicesImpl::init()
{
    auto barrier = std::make_shared<sdbusplus::async::barrier>(6);

    ctx.spawn(watchHostInterfacesAdded(barrier));
    ctx.spawn(watchHostStatePropertiesChanged(barrier));
    ctx.spawn(watchBootProgressPropertiesChanged(barrier));
    ctx.spawn(watchPairingInterfacesAdded(barrier));
    ctx.spawn(watchPairingPropertiesChanged(barrier));

    co_await barrier->wait();

    co_await readHostState();
    co_await readBootProgress();
    co_await readPairingProperties();
    updateSystemState();

    co_await waitForSystemInventoryPath();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::readHostState()
{
    try
    {
        auto service = co_await util::getService(ctx, object_path::hostState,
                                                 HostState::interface);
        hostState = co_await HostState(ctx)
                        .service(service)
                        .path(object_path::hostState)
                        .current_host_state();

        lg2::debug("initial host state is {STATE}", "STATE", hostState.value());
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Not on D-Bus
    }

    co_return;
}

sdbusplus::async::task<> ServicesImpl::watchHostInterfacesAdded(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx, rules::interfacesAddedAtPath(object_path::hostState));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        bool changed = false;

        auto [_, interfaces] =
            co_await match
                .next<sdbusplus::message::object_path, HostInterfaceMap>();

        auto it = interfaces.find(HostState::interface);
        if (it != interfaces.end())
        {
            hostState = std::get<HostState::HostState>(
                it->second.at("CurrentHostState"));

            lg2::debug("The added Host state is {STATE}", "STATE",
                       hostState.value());
            changed = true;
        }

        it = interfaces.find(BootProgress::interface);
        if (it != interfaces.end())
        {
            bootProgress = std::get<BootProgress::ProgressStages>(
                it->second.at("BootProgress"));

            lg2::debug("The added BootProgress is {PROGRESS}", "PROGRESS",
                       bootProgress.value());
            changed = true;
        }

        if (changed)
        {
            updateSystemState();
        }
    }

    co_return;
}

sdbusplus::async::task<> ServicesImpl::watchHostStatePropertiesChanged(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx,
        rules::propertiesChanged(object_path::hostState, HostState::interface));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_, properties] = co_await match.next<std::string, HostPropMap>();

        auto it = properties.find("CurrentHostState");
        if (it != properties.end())
        {
            hostState = std::get<HostState::HostState>(it->second);

            lg2::debug("Host state changed to {STATE}", "STATE",
                       hostState.value());

            updateSystemState();
        }
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::readBootProgress()
{
    try
    {
        auto service = co_await util::getService(ctx, object_path::hostState,
                                                 BootProgress::interface);
        bootProgress = co_await BootProgress(ctx)
                           .service(service)
                           .path(object_path::hostState)
                           .boot_progress();

        lg2::debug("Initial BootProgress is {PROGRESS}", "PROGRESS",
                   bootProgress.value());
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Not on D-Bus yet
    }

    co_return;
}

sdbusplus::async::task<> ServicesImpl::readPairingProperties()
{
    try
    {
        auto service = co_await util::getService(ctx, Pairing::instance_path,
                                                 Pairing::interface);
        auto props = co_await Pairing(ctx)
                         .service(service)
                         .path(Pairing::instance_path)
                         .properties();

        paired = props.provisioned;
        peerConnected = props.peer_connected == PeerConnectionStatus::Connected;

        lg2::debug("Initial Paired = {PAIR} and PeerConnected = {STATUS}",
                   "PAIR", props.provisioned, "STATUS", props.peer_connected);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Not on D-Bus yet
    }
}

sdbusplus::async::task<> ServicesImpl::watchBootProgressPropertiesChanged(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx, rules::propertiesChanged(object_path::hostState,
                                      BootProgress::interface));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_, properties] = co_await match.next<std::string, HostPropMap>();

        auto it = properties.find("BootProgress");
        if (it != properties.end())
        {
            bootProgress = std::get<BootProgress::ProgressStages>(it->second);

            lg2::debug("BootProgress changed to {PROGRESS}", "PROGRESS",
                       bootProgress.value());

            updateSystemState();
        }
    }

    co_return;
}

void ServicesImpl::loadPairingProps(const PairingPropMap& propertyMap)
{
    auto it = propertyMap.find("PeerConnected");
    if (it != propertyMap.end())
    {
        auto prevConnected = peerConnected;

        auto rawStatus = std::get<Pairing::PeerConnectionStatus>(it->second);

        lg2::info("The new PeerConnected value is {STATUS}", "STATUS",
                  rawStatus);

        peerConnected = rawStatus == PeerConnectionStatus::Connected;

        // Ignore the intermediate states when doing callbacks to deal
        // with Connected->InProgress->Connected flapping.
        if (prevConnected != peerConnected &&
            (rawStatus == PeerConnectionStatus::Connected ||
             rawStatus == PeerConnectionStatus::NotConnected))
        {
            std::ranges::for_each(peerConnectedCBs, [this](const auto& entry) {
                entry.second(peerConnected);
            });
        }
    }

    it = propertyMap.find("Provisioned");
    if (it != propertyMap.end())
    {
        auto prevPaired = paired;
        paired = std::get<bool>(it->second);

        lg2::info("The new Paired value is {PROV}", "PROV", paired);

        // Invoke callbacks if value changed
        if (prevPaired != paired)
        {
            std::ranges::for_each(pairedCBs, [this](const auto& entry) {
                entry.second(paired);
            });
        }
    }
}

sdbusplus::async::task<> ServicesImpl::watchPairingInterfacesAdded(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx, rules::interfacesAddedAtPath(Pairing::instance_path));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_, interfaces] =
            co_await match
                .next<sdbusplus::message::object_path, PairingInterfaceMap>();

        auto it = interfaces.find(Pairing::interface);
        if (it != interfaces.end())
        {
            lg2::info("Pairing interface added");
            loadPairingProps(it->second);
        }
    }
}

sdbusplus::async::task<> ServicesImpl::watchPairingPropertiesChanged(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx,
        rules::propertiesChanged(Pairing::instance_path, Pairing::interface));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_,
              properties] = co_await match.next<std::string, PairingPropMap>();
        loadPairingProps(properties);
    }
}

void ServicesImpl::updateSystemState()
{
    if (!hostState.has_value() || !bootProgress.has_value())
    {
        lg2::debug("Cannot calculate system state yet");
        return;
    }

    SystemState newState =
        calculateSystemState(hostState.value(), bootProgress.value());

    lg2::info("Calculated system state is {STATE}", "STATE",
              getSystemStateName(newState));

    if (!systemState.has_value() || (newState != systemState.value()))
    {
        if (systemState.has_value())
        {
            lg2::debug("System state changing from {OLD_STATE} to {NEW_STATE}",
                       "OLD_STATE", getSystemStateName(systemState.value()),
                       "NEW_STATE", getSystemStateName(newState));
        }
        systemState = newState;
        std::ranges::for_each(systemStateCBs, [newState](const auto& entry) {
            entry.second(newState);
        });
    }
}

std::optional<size_t> ServicesImpl::getBMCPosition() const
{
    static std::optional<size_t> bmcPosition;
    const std::filesystem::path posFile{"/run/openbmc/bmc_position"};

    if (bmcPosition.has_value())
    {
        return bmcPosition;
    }

    std::error_code ec;
    if (!std::filesystem::exists(posFile, ec))
    {
        lg2::error("BMC position file {FILE} doesn't exist", "FILE", posFile);
        return std::nullopt;
    }

    std::ifstream stream{posFile};
    if (!stream)
    {
        lg2::error("Could not open BMC position file {FILE}", "FILE", posFile);
        return std::nullopt;
    }

    size_t position;
    stream >> position;

    if (stream.fail())
    {
        lg2::error("Failed reading BMC position out of {FILE}", "FILE",
                   posFile);
        return std::nullopt;
    }

    if (position == std::numeric_limits<size_t>::max())
    {
        lg2::warning("BMC position value could not be obtained from hardware");
        return std::nullopt;
    }

    // Don't cache it until there is a good value.
    bmcPosition = position;

    return bmcPosition;
}

// NOLINTBEGIN
sdbusplus::async::task<sdbusplus::message::object_path>
    ServicesImpl::getUnitPath(const std::string& unitName) const
// NOLINTEND
{
    constexpr auto systemd = sdbusplus::async::proxy()
                                 .service(service::systemd)
                                 .path(object_path::systemd)
                                 .interface(interface::systemdMgr);

    co_return co_await systemd.call<sdbusplus::message::object_path>(
        ctx, "GetUnit", unitName);
}

// NOLINTBEGIN
sdbusplus::async::task<std::string> ServicesImpl::getUnitState(
    const std::string& unitName) const
// NOLINTEND
{
    try
    {
        auto unitPath = co_await getUnitPath(unitName);

        auto systemd = sdbusplus::async::proxy()
                           .service(service::systemd)
                           .path(unitPath.str)
                           .interface(interface::systemdUnit);
        auto state =
            co_await systemd.get_property<std::string>(ctx, "ActiveState");

        co_return state;
    }
    catch (const sdbusplus::exception_t& e)
    {
        // For some units systemd returns NoSuchUnit if it isn't running.
        if ((e.name() == nullptr) ||
            (std::string{e.name()} != "org.freedesktop.systemd1.NoSuchUnit"))
        {
            lg2::error(
                "Unable to determine if {UNIT} is running: {ERROR}. Assuming it isn't.",
                "UNIT", unitName, "ERROR", e.what());
        }
        else
        {
            lg2::debug("Got a NoSuchUnit error for {UNIT}", "UNIT", unitName);
        }
    }

    co_return "inactive";
}

sdbusplus::async::task<> ServicesImpl::listAndLogSystemdJobs() const
{
    try
    {
        constexpr auto systemd = sdbusplus::async::proxy()
                                     .service(service::systemd)
                                     .path(object_path::systemd)
                                     .interface(interface::systemdMgr);

        using JobInfo = std::tuple<uint32_t, std::string, std::string,
                                   std::string, sdbusplus::message::object_path,
                                   sdbusplus::message::object_path>;

        auto jobs =
            co_await systemd.call<std::vector<JobInfo>>(ctx, "ListJobs");

        lg2::error("Active systemd jobs at timeout ({COUNT} total):", "COUNT",
                   jobs.size());

        for (const auto& [id, name, type, state, jobPath, unitPath] : jobs)
        {
            lg2::error("  Job {ID}: {NAME} State: {STATE}", "ID", id, "NAME",
                       name, "STATE", state);
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Failed to list systemd jobs: {ERROR}", "ERROR", e);
    }
}

sdbusplus::async::task<> ServicesImpl::startUnit(
    const std::string& unitName, std::chrono::seconds timeout) const
{
    using namespace std::chrono_literals;

    auto currentState = co_await getUnitState(unitName);
    if (currentState == "active")
    {
        lg2::info("Unit {UNIT} is already active, not starting again", "UNIT",
                  unitName);
        co_return;
    }

    constexpr auto systemd = sdbusplus::async::proxy()
                                 .service(service::systemd)
                                 .path(object_path::systemd)
                                 .interface(interface::systemdMgr);

    // Don't need to start the unit if activating, but still
    // need to wait for it to complete.
    if (currentState != "activating")
    {
        lg2::info("Starting unit {UNIT}", "UNIT", unitName);

        co_await systemd.call<sdbusplus::message::object_path>(
            ctx, "StartUnit", unitName, std::string{"replace"});
    }
    else
    {
        lg2::info("Unit is already activating");
    }

    std::string state;
    auto end = std::chrono::steady_clock::now() + timeout;

    WaitTracker::WaitGuard guard(waitTracker, WaitOperation::startUnit,
                                 timeout);

    while ((state != "active") && (state != "failed"))
    {
        if (std::chrono::steady_clock::now() > end)
        {
            lg2::error("Timed out waiting for {UNIT} to start after {TIME}s",
                       "UNIT", unitName, "TIME", timeout.count());
            co_await listAndLogSystemdJobs();
            break;
        }

        co_await sdbusplus::async::sleep_for(ctx, 1s);
        state = co_await getUnitState(unitName);
    }

    lg2::info("Finished waiting for {UNIT} to start (result = {STATE})", "UNIT",
              unitName, "STATE", state);

    if (state != "active")
    {
        throw std::runtime_error{
            std::format("{} final state is {}", unitName, state)};
    }
}

bool ServicesImpl::getPaired() const
{
    return paired;
}

std::string ServicesImpl::getFWVersion() const
{
    static std::string hexVersionString;

    if (!hexVersionString.empty())
    {
        return hexVersionString;
    }

    std::ifstream versionFile{"/etc/os-release"};
    std::string line;
    std::string keyPattern{"VERSION_ID="};
    std::string version;

    while (std::getline(versionFile, line))
    {
        // Handle either quotes or no quotes around the value
        if (line.substr(0, keyPattern.size()).find(keyPattern) !=
            std::string::npos)
        {
            // If the value isn't surrounded by quotes, then pos will be
            // npos + 1 = 0, and the 2nd arg to substr() will be npos
            // which means get the rest of the string.
            auto value = line.substr(keyPattern.size());
            std::size_t pos = value.find_first_of('"') + 1;
            version = value.substr(pos, value.find_last_of('"') - pos);
            break;
        }
    }

    if (version.empty())
    {
        lg2::error("Unable to parse VERSION_ID out of /etc/os-release");
        // let it hash the empty string
    }

    using EVP_MD_CTX_Ptr =
        std::unique_ptr<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    EVP_MD_CTX_Ptr context(EVP_MD_CTX_new(), &::EVP_MD_CTX_free);

    EVP_DigestInit(context.get(), EVP_sha512());
    EVP_DigestUpdate(context.get(), version.c_str(), strlen(version.c_str()));
    EVP_DigestFinal(context.get(), digest.data(), nullptr);

    hexVersionString = std::format("{:02X}{:02X}{:02X}{:02X}", digest[0],
                                   digest[1], digest[2], digest[3]);
    return hexVersionString;
}

SystemState ServicesImpl::getSystemState() const
{
    if (systemState.has_value())
    {
        return systemState.value();
    }

    throw std::runtime_error("System state not available");
}

// NOLINTNEXTLINE
auto ServicesImpl::getBMCState() const -> sdbusplus::async::task<
    sdbusplus::common::xyz::openbmc_project::state::BMC::BMCState>
{
    using StateMgr = sdbusplus::client::xyz::openbmc_project::state::BMC<>;

    std::string statePath = std::string{StateMgr::namespace_path::value} + '/' +
                            StateMgr::namespace_path::bmc;
    auto service =
        co_await util::getService(ctx, statePath, StateMgr::interface);

    auto stateMgr = StateMgr(ctx).service(service).path(statePath);
    co_return co_await stateMgr.current_bmc_state();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::doFailoverImminentDelay() const
{
    lg2::info("Delaying for 10s for failover imminent notification");
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    co_await sdbusplus::async::sleep_for(ctx, std::chrono::seconds{10});
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::flushJournal() const
{
    try
    {
        lg2::info("Starting journal flush");
        auto rc = co_await util::runAsyncCmd(ctx, "/usr/bin/journalctl --sync");
        lg2::info("Completed journal flush with rc {RC}", "RC", rc);
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception while syncing journal: {ERROR}", "ERROR", e);
    }
}

sdbusplus::async::task<> ServicesImpl::waitForSystemInventoryPath()
{
    using namespace std::chrono_literals;
    constexpr auto timeout = 3min;

    WaitTracker::WaitGuard guard(
        waitTracker, WaitOperation::systemInventoryPath,
        std::chrono::duration_cast<std::chrono::seconds>(timeout));

    auto end = std::chrono::steady_clock::now() + timeout;
    bool traced = false;

    while (std::chrono::steady_clock::now() < end)
    {
        try
        {
            systemInvPath = co_await util::findSystemInventoryPath(ctx);
            co_return;
        }
        catch (const std::invalid_argument& e)
        {
            // Wrong number of system interfaces.
            lg2::error("Error obtaining system inventory path: {ERROR}",
                       "ERROR", e);
            co_return;
        }
        catch (const std::exception& e)
        {
            if (!traced)
            {
                traced = true;
                lg2::info("Waiting for system inventory object. ({ERROR})",
                          "ERROR", e);
            }
        }

        co_await sdbusplus::async::sleep_for(ctx, 1s);
    }

    lg2::error("Timed out waiting for system inventory object");
}

sdbusplus::async::task<bool> ServicesImpl::checkSystemInventoryStatus()
{
    if (systemInvPath.empty())
    {
        lg2::error("There is no system inventory object");
        co_return false;
    }

    using namespace std::chrono_literals;
    constexpr auto timeout = 3min;

    WaitTracker::WaitGuard guard(
        waitTracker, WaitOperation::systemInventoryStatus,
        std::chrono::duration_cast<std::chrono::seconds>(timeout));

    bool tracedWait = false;
    std::string service;
    auto end = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < end)
    {
        InvProgress::OperationStatus status;

        try
        {
            if (service.empty())
            {
                service = co_await util::getService(ctx, systemInvPath,
                                                    InvProgress::interface);
            }

            status = co_await InvProgress(ctx)
                         .service(service)
                         .path(systemInvPath)
                         .status();
        }
        catch (const std::exception& e)
        {
            // If the system object path is known but it doesn't have
            // the Progress interface, assume it isn't used in this system.
            lg2::warning(
                "Progress interface not available on {PATH}, assuming it isn't implemented. Error = {ERROR}",
                "PATH", systemInvPath, "ERROR", e);
            co_return true;
        }

        if (status == InvProgress::OperationStatus::Completed)
        {
            lg2::info("System inventory status is complete");
            co_return true;
        }
        else if ((status == InvProgress::OperationStatus::Failed) ||
                 (status == InvProgress::OperationStatus::Aborted))
        {
            lg2::error("System inventory failed with status {STATUS}", "STATUS",
                       status);
            co_return false;
        }
        else
        {
            if (!tracedWait)
            {
                tracedWait = true;
                lg2::info(
                    "System inventory status isn't complete yet: {STATUS}",
                    "STATUS", status);
            }
        }

        co_await sdbusplus::async::sleep_for(ctx, 1s);
    }

    lg2::error("Timed out waiting for system inventory status");
    co_return false;
}

sdbusplus::async::task<> ServicesImpl::acquireFullHardwareAccess()
{
    using Options = std::map<std::string, std::variant<bool>>;
    Options options;

    options.emplace(SidebandBus::convertAcquireOptionsToString(
                        SidebandBus::AcquireOptions::Force),
                    true);

    co_await SidebandBus(ctx)
        .service(SidebandBus::interface)
        .path(SidebandBus::instance_path)
        .acquire(options);
}

sdbusplus::async::task<> ServicesImpl::logError(
    std::string error, errors::Level severity,
    errors::AdditionalData data) const
{
    try
    {
        using Create =
            sdbusplus::client::xyz::openbmc_project::logging::Create<>;

        co_await Create(ctx)
            .service(Create::default_service)
            .path(Create::instance_path)
            .create(error, severity, data);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to create event log {MSG}: {ERROR}", "MSG", error,
                   "ERROR", e);
    }
}

sdbusplus::async::task<> ServicesImpl::waitForPeerConnection(
    AbortPredicate shouldAbort)
{
    using namespace std::chrono_literals;
    constexpr auto timeout = 10min;

    lg2::info("waitForPeerConnection initial peerConnected value = {STATUS}",
              "STATUS", peerConnected);

    if (peerConnected)
    {
        co_return;
    }

    WaitTracker::WaitGuard guard(
        waitTracker, WaitOperation::peerConnection,
        std::chrono::duration_cast<std::chrono::seconds>(timeout));

    auto end = std::chrono::steady_clock::now() + timeout;
    bool tracedWait = false;

    while (std::chrono::steady_clock::now() < end)
    {
        if (peerConnected)
        {
            lg2::info("Peer now connected");
            co_return;
        }

        if (shouldAbort && shouldAbort())
        {
            lg2::info("Peer connection wait no longer necessary");
            co_return;
        }

        if (!tracedWait)
        {
            tracedWait = true;
            lg2::info("Waiting up to {MIN} minutes for peer connection", "MIN",
                      timeout.count());
        }

        co_await sdbusplus::async::sleep_for(ctx, 500ms);
    }

    lg2::error("Timed out waiting for peer connection after {MIN} minutes",
               "MIN", timeout.count());
}

sdbusplus::async::task<> ServicesImpl::waitForSelfPairing()
{
    using namespace std::chrono_literals;
    constexpr auto timeout = 30s;

    WaitTracker::WaitGuard guard(
        waitTracker, WaitOperation::selfPairing,
        std::chrono::duration_cast<std::chrono::seconds>(timeout));

    auto end = std::chrono::steady_clock::now() + timeout;
    bool traced = false;

    while (std::chrono::steady_clock::now() < end)
    {
        if (paired)
        {
            co_return;
        }

        if (!traced)
        {
            traced = true;
            lg2::info("Waiting up to 30s for self pairing");
        }

        co_await sdbusplus::async::sleep_for(ctx, 1s);
    }

    lg2::warning("Timed out waiting for self pairing");
}

void ServicesImpl::setRedundancyDetermined()
{
    namespace fs = std::filesystem;

    const fs::path runDir = "/run/openbmc";
    const fs::path markerFile = runDir / "bmc_redundancy_determined";

    try
    {
        if (!fs::exists(runDir))
        {
            fs::create_directories(runDir);
        }

        std::ofstream file(markerFile);
        if (!file)
        {
            lg2::error(
                "Failed to create redundancy determined marker file {FILE}",
                "FILE", markerFile.string());
            return;
        }
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception creating redundancy determined marker file: {ERROR}",
            "ERROR", e);
    }
}

} // namespace rbmc
