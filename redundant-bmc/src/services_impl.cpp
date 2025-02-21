/* SPDX-License-Identifier: Apache-2.0 */
#include "services_impl.hpp"

#include <openssl/evp.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>
#include <xyz/openbmc_project/State/Host/client.hpp>

#include <fstream>

namespace rbmc
{

using HostState = sdbusplus::client::xyz::openbmc_project::state::Host<>;
using ObjectMapper = sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

using HostProperties =
    std::variant<std::string, HostState::HostState, HostState::RestartCause,
                 HostState::Transition, std::set<HostState::Transition>>;
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

bool getPoweredOnValue(HostState::HostState state)
{
    // For the current purposes, consider all other values
    // as On. May need to revisit in the future.
    return state != HostState::HostState::Off;
}

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

} // namespace util

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::init()
{
    ctx.spawn(watchHostInterfacesAdded());
    ctx.spawn(watchHostPropertiesChanged());
    co_await readHostState();
    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::readHostState()
{
    try
    {
        auto service = co_await util::getService(ctx, object_path::hostState,
                                                 HostState::interface);
        auto stateMgr =
            HostState(ctx).service(service).path(object_path::hostState);

        auto hostState = co_await stateMgr.current_host_state();
        poweredOn = util::getPoweredOnValue(hostState);

        lg2::debug("initial host state is {STATE}", "STATE", hostState);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Not on D-Bus
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::watchHostInterfacesAdded()
{
    sdbusplus::async::match match(
        ctx, rules::interfacesAddedAtPath(object_path::hostState));

    while (!ctx.stop_requested())
    {
        auto [_, interfaces] =
            co_await match
                .next<sdbusplus::message::object_path, HostInterfaceMap>();

        auto it = interfaces.find(HostState::interface);
        if (it != interfaces.end())
        {
            auto hostState = std::get<HostState::HostState>(
                it->second.at("CurrentHostState"));
            poweredOn = util::getPoweredOnValue(hostState);

            lg2::debug("The added Host state is {STATE}", "STATE", hostState);
        }
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> ServicesImpl::watchHostPropertiesChanged()
{
    sdbusplus::async::match match(
        ctx,
        rules::propertiesChanged(object_path::hostState, HostState::interface));

    while (!ctx.stop_requested())
    {
        auto [_, properties] = co_await match.next<std::string, HostPropMap>();

        auto it = properties.find("CurrentHostState");
        if (it != properties.end())
        {
            auto hostState = std::get<HostState::HostState>(it->second);
            poweredOn = util::getPoweredOnValue(hostState);

            lg2::debug("Host state changed to {STATE}", "STATE", hostState);
        }
    }

    co_return;
}

size_t ServicesImpl::getBMCPosition() const
{
    size_t bmcPosition = 0;

    // NOTE:  This a temporary solution for simulation until the
    // daemon that should be providing this information is in place.
    // Most likely, this will then have to return a task<uint32_t>.

    // Read it out of the bmc_position uboot environment variable
    // This was written by a simulation script.
    std::string cmd{"/sbin/fw_printenv -n bmc_position"};

    // NOLINTBEGIN(cert-env33-c)
    FILE* pipe = popen(cmd.c_str(), "r");
    // NOLINTEND(cert-env33-c)
    if (pipe == nullptr)
    {
        throw std::runtime_error("Error calling popen to get bmc_position");
    }

    std::string output;
    std::array<char, 128> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        output.append(buffer.data());
    }

    int rc = pclose(pipe);
    if (WEXITSTATUS(rc) != 0)
    {
        throw std::runtime_error{std::format(
            "Error running cmd: {}, output = {}, rc = {}", cmd, output, rc)};
    }

    auto [_,
          ec] = std::from_chars(&*output.begin(), &*output.end(), bmcPosition);
    if (ec != std::errc())
    {
        throw std::runtime_error{
            std::format("Could not extract position from {}: rc {}", output,
                        std::to_underlying(ec))};
    }

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

// NOLINTBEGIN
sdbusplus::async::task<> ServicesImpl::startUnit(
    const std::string& unitName) const
// NOLINTEND
{
    using namespace std::chrono_literals;
    constexpr auto systemd = sdbusplus::async::proxy()
                                 .service(service::systemd)
                                 .path(object_path::systemd)
                                 .interface(interface::systemdMgr);

    lg2::info("Starting unit {UNIT}", "UNIT", unitName);

    co_await systemd.call<sdbusplus::message::object_path>(
        ctx, "StartUnit", unitName, std::string{"replace"});

    std::string state;

    while ((state != "active") && (state != "failed"))
    {
        co_await sdbusplus::async::sleep_for(ctx, 1s);
        state = co_await getUnitState(unitName);
    }

    lg2::info("Finished waiting for {UNIT} to start (result = {STATE})", "UNIT",
              unitName, "STATE", state);

    co_return;
}

bool ServicesImpl::getProvisioned() const
{
    // TODO: Eventually get this from somewhere.
    return true;
}

std::string ServicesImpl::getFWVersion() const
{
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

    return std::format("{:02X}{:02X}{:02X}{:02X}", digest[0], digest[1],
                       digest[2], digest[3]);
}

bool ServicesImpl::isPoweredOn() const
{
    if (poweredOn.has_value())
    {
        return poweredOn.value();
    }

    throw std::runtime_error("Power state not available");
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

} // namespace rbmc
