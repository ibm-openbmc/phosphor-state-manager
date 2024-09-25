/* SPDX-License-Identifier: Apache-2.0 */
#include "services_impl.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

namespace object_path
{
constexpr auto systemd = "/org/freedesktop/systemd1";
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
sdbusplus::async::task<std::string>
    ServicesImpl::getUnitState(const std::string& unitName) const
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
sdbusplus::async::task<>
    ServicesImpl::startUnit(const std::string& unitName) const
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

} // namespace rbmc
