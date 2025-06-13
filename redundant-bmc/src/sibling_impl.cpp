/* SPDX-License-Identifier: Apache-2.0 */
#include "sibling_impl.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/Software/Version/common.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>
#include <xyz/openbmc_project/State/Decorator/Heartbeat/common.hpp>

#include <ranges>

namespace rbmc
{

using RedIntf = sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;
using VersionIntf = sdbusplus::common::xyz::openbmc_project::software::Version;
using BMCStateIntf = sdbusplus::common::xyz::openbmc_project::state::BMC;
using HBIntf =
    sdbusplus::common::xyz::openbmc_project::state::decorator::Heartbeat;

SiblingImpl::SiblingImpl(sdbusplus::async::context& ctx) :
    ctx(ctx), objectPath(std::string{RedIntf::namespace_path::value} + '/' +
                         RedIntf::namespace_path::sibling_bmc)
{}

bool SiblingImpl::isBMCPresent()
{
    // TODO: Actually check this.
    // May also need to check if it has Vcs PGOOD.
    return true;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::init()
{
    if (initialized)
    {
        lg2::info("Sibling::init called more than once");
        co_return;
    }

    // Start the D-Bus watches for the signals that don't
    // need a service name.
    ctx.spawn(watchInterfaceAdded());
    ctx.spawn(watchInterfaceRemoved());
    ctx.spawn(watchPropertyChanged());

    // Attempt to get the D-Bus service name.  It would only
    // be there if the sibling BMC is present.
    serviceName = co_await getServiceName();

    if (!serviceName.empty())
    {
        ctx.spawn(watchNameOwnerChanged());

        co_await initProperties();
    }

    lg2::info("In Sibling init, interface present is {PRESENT}", "PRESENT",
              getInterfacePresent());

    initialized = true;
}

// NOLINTNEXTLINE
sdbusplus::async::task<std::string> SiblingImpl::getServiceName()
{
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;
    std::vector<std::string> interface{RedIntf::interface};

    try
    {
        auto object = co_await ObjectMapper(ctx)
                          .service(ObjectMapper::default_service)
                          .path(ObjectMapper::instance_path)
                          .get_object(objectPath, interface);

        co_return object.begin()->first;
    }
    catch (const sdbusplus::exception_t&)
    {}

    co_return std::string{};
}

void SiblingImpl::loadRedundancyProps(
    const SiblingImpl::PropertyMap& propertyMap)
{
    redundancy.present = true;

    auto it = propertyMap.find("RedundancyEnabled");
    if (it != propertyMap.end())
    {
        auto old = redundancy.redundancyEnabled;
        redundancy.redundancyEnabled = std::get<bool>(it->second);
        if (redundancy.redundancyEnabled != old)
        {
            for (const auto& callback :
                 std::ranges::views::values(redEnabledCBs))
            {
                callback(redundancy.redundancyEnabled);
            }
        }
    }

    it = propertyMap.find("FailoversAllowed");
    if (it != propertyMap.end())
    {
        auto old = redundancy.failoversAllowed;
        redundancy.failoversAllowed = std::get<bool>(it->second);
        if (redundancy.failoversAllowed != old)
        {
            for (const auto& callback :
                 std::ranges::views::values(foAllowedCBs))
            {
                callback(redundancy.failoversAllowed);
            }
        }
    }

    it = propertyMap.find("Role");
    if (it != propertyMap.end())
    {
        redundancy.role = std::get<Role>(it->second);
    }
}

void SiblingImpl::loadVersionProps(const SiblingImpl::PropertyMap& propertyMap)
{
    version.present = true;

    auto it = propertyMap.find("Version");
    if (it != propertyMap.end())
    {
        version.version = std::get<std::string>(it->second);
    }
}

void SiblingImpl::loadStateProps(const SiblingImpl::PropertyMap& propertyMap)
{
    bmcState.present = true;

    auto it = propertyMap.find("CurrentBMCState");
    if (it != propertyMap.end())
    {
        bmcState.state = std::get<BMCState>(it->second);
    }
}

void SiblingImpl::loadHeartbeatProps(
    const SiblingImpl::PropertyMap& propertyMap)
{
    heartbeat.present = true;

    auto it = propertyMap.find("Active");
    if (it != propertyMap.end())
    {
        auto old = heartbeat.active;
        heartbeat.active = std::get<bool>(it->second);
        if (heartbeat.active != old)
        {
            for (const auto& callback :
                 std::ranges::views::values(heartbeatCBs))
            {
                callback(heartbeat.active);
            }
        }
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::watchInterfaceAdded()
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(ctx,
                                  rules::interfacesAddedAtPath(objectPath));
    while (!ctx.stop_requested())
    {
        auto [_, interfaces] =
            co_await match
                .next<sdbusplus::message::object_path, InterfaceMap>();

        std::ranges::for_each(interfaces, [this](const auto& entry) {
            loadFromPropertyMap(entry.first, entry.second);
        });

        // If first time seen, wait for the service name to get into
        // the mapper and then start the nameOwnerChanged watch.
        if (serviceName.empty())
        {
            const size_t mapperRetries = 200;
            size_t count = 0;

            do
            {
                using namespace std::chrono_literals;
                co_await sdbusplus::async::sleep_for(ctx, 100ms);
                serviceName = co_await getServiceName();
                lg2::info("After interfacesAdded, sibling service is {SERVICE}",
                          "SERVICE", serviceName);
            } while (serviceName.empty() && (count++ < mapperRetries));

            if (!serviceName.empty())
            {
                ctx.spawn(watchNameOwnerChanged());
            }
        }
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::watchInterfaceRemoved()
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(ctx,
                                  rules::interfacesRemovedAtPath(objectPath));

    while (!ctx.stop_requested())
    {
        auto [_, interfaces] =
            co_await match.next<sdbusplus::message::object_path,
                                std::vector<std::string>>();

        if (std::ranges::contains(interfaces, RedIntf::interface))
        {
            redundancy.present = false;
        }
        if (std::ranges::contains(interfaces, VersionIntf::interface))
        {
            redundancy.present = false;
        }
        if (std::ranges::contains(interfaces, BMCStateIntf::interface))
        {
            bmcState.present = false;
        }
        if (std::ranges::contains(interfaces, HBIntf::interface))
        {
            heartbeat.present = false;

            auto old = heartbeat.active;
            heartbeat.active = false;
            if (old != heartbeat.active)
            {
                for (const auto& callback :
                     std::ranges::views::values(heartbeatCBs))
                {
                    callback(heartbeat.active);
                }
            }
        }
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::watchPropertyChanged()
{
    sdbusplus::async::match match(
        ctx, std::format("type='signal',member='PropertiesChanged',path='{}'",
                         objectPath));

    while (!ctx.stop_requested())
    {
        auto [iface,
              propertyMap] = co_await match.next<std::string, PropertyMap>();

        for (const auto& name : std::views::keys(propertyMap))
        {
            lg2::info("Sibling property {PROP} changed", "PROP", name);
        }

        loadFromPropertyMap(iface, propertyMap);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::watchNameOwnerChanged()
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(ctx, rules::nameOwnerChanged(serviceName));

    while (!ctx.stop_requested())
    {
        auto [name, oldOwner, newOwner] =
            co_await match.next<std::string, std::string, std::string>();

        if (!oldOwner.empty() && newOwner.empty())
        {
            lg2::info("Sibling D-Bus name lost");
            setInterfacesNotPresent();
        }
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::initProperties()
{
    auto sibling = sdbusplus::async::proxy()
                       .service(serviceName)
                       .path(RedIntf::namespace_path::value)
                       .interface("org.freedesktop.DBus.ObjectManager");
    try
    {
        auto objects =
            co_await sibling.call<ManagedObjects>(ctx, "GetManagedObjects");

        auto object = objects.find(objectPath);
        if (object != objects.end())
        {
            std::ranges::for_each(
                object->second, [this](const auto& interface) {
                    loadFromPropertyMap(interface.first, interface.second);
                });
        }
    }
    catch (const sdbusplus::exception_t&)
    {
        // Not on D-Bus yet
        setInterfacesNotPresent();
    }
}

void SiblingImpl::loadFromPropertyMap(const std::string& interface,
                                      const PropertyMap& propertyMap)
{
    if (interface == RedIntf::interface)
    {
        loadRedundancyProps(propertyMap);
    }
    else if (interface == BMCStateIntf::interface)
    {
        loadStateProps(propertyMap);
    }
    else if (interface == VersionIntf::interface)
    {
        loadVersionProps(propertyMap);
    }
    else if (interface == HBIntf::interface)
    {
        loadHeartbeatProps(propertyMap);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::waitForSiblingUp()
{
    using namespace std::chrono_literals;
    auto start = std::chrono::steady_clock::now();
    std::chrono::minutes timeout{6};
    auto waiting = false;

    while ((!getInterfacePresent() || !hasHeartbeat()) &&
           ((std::chrono::steady_clock::now() - start) < timeout))
    {
        if (!waiting)
        {
            lg2::info(
                "Waiting up to {TIME} minutes for sibling interface and/or heartbeat: "
                "Present = {PRES}, Heartbeat = {HB}",
                "TIME", timeout.count(), "PRES", getInterfacePresent(), "HB",
                heartbeat.active);
            waiting = true;
        }

        co_await sdbusplus::async::sleep_for(ctx, 500ms);
    }

    lg2::info(
        "Done waiting for sibling. Interface present = {PRES}, heartbeat = {HB}",
        "PRES", getInterfacePresent(), "HB", heartbeat.active);
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::waitForSiblingRole()
{
    using namespace std::chrono_literals;
    std::chrono::seconds timeout{10};
    bool waiting = false;

    auto noRole = [this]() {
        return getRole().value_or(Role::Unknown) == Role::Unknown;
    };

    if (!hasHeartbeat() || !noRole())
    {
        co_return;
    }

    auto start = std::chrono::steady_clock::now();

    while (noRole() && ((std::chrono::steady_clock::now() - start) < timeout))
    {
        if (!waiting)
        {
            waiting = true;
            lg2::info("Waiting up to {TIME}s for sibling role", "TIME",
                      timeout.count());
        }

        co_await sdbusplus::async::sleep_for(ctx, 500ms);
    }
}

// NOLINTBEGIN(clang-analyzer-core.uninitialized.Branch,
//             readability-static-accessed-through-instance)
sdbusplus::async::task<> SiblingImpl::waitForBMCSteadyState() const
{
    using namespace std::chrono_literals;
    auto start = std::chrono::steady_clock::now();
    std::chrono::minutes timeout{10};
    bool waiting = false;

    // If sibling isn't alive don't bother waiting
    if (!hasHeartbeat())
    {
        co_return;
    }

    auto steadyState = [](BMCState state) {
        return (state == BMCState::Ready) || (state == BMCState::Quiesced);
    };

    while (!steadyState(bmcState.state) &&
           ((std::chrono::steady_clock::now() - start) < timeout))
    {
        if (!waiting)
        {
            lg2::info(
                "Waiting up to {TIME} minutes for sibling BMC steady state.",
                "TIME", timeout.count());
            waiting = true;
        }

        co_await sdbusplus::async::sleep_for(ctx, 500ms);
    }

    lg2::info("Done waiting for sibling steady state. State = {STATE}", "STATE",
              bmcState.state);
}
// NOLINTEND(clang-analyzer-core.uninitialized.Branch,
//           readability-static-accessed-through-instance)

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::pauseForHeartbeatChange() const
{
    using namespace std::chrono_literals;
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    co_return co_await sdbusplus::async::sleep_for(ctx, 5s);
}

} // namespace rbmc
