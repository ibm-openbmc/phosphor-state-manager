/* SPDX-License-Identifier: Apache-2.0 */
#include "sibling_impl.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/Software/Version/common.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/common.hpp>
#include <xyz/openbmc_project/State/BMC/common.hpp>
#include <xyz/openbmc_project/State/Decorator/Availability/client.hpp>

#include <chrono>
#include <ranges>

namespace rbmc
{

using RedIntf = sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;
using VersionIntf = sdbusplus::common::xyz::openbmc_project::software::Version;
using BMCStateIntf = sdbusplus::common::xyz::openbmc_project::state::BMC;
using AvailIntf =
    sdbusplus::common::xyz::openbmc_project::state::decorator::Availability;

SiblingImpl::SiblingImpl(sdbusplus::async::context& ctx) :
    ctx(ctx), objectPath(std::string{RedIntf::namespace_path::value} + '/' +
                         RedIntf::namespace_path::sibling_bmc)
{}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::init()
{
    if (initialized)
    {
        lg2::warning("Sibling::init called more than once");
        co_return;
    }

    auto barrier = std::make_shared<sdbusplus::async::barrier>(4);

    // Start the D-Bus watches for the signals that don't
    // need a service name.
    ctx.spawn(watchInterfaceAdded(barrier));
    ctx.spawn(watchInterfaceRemoved(barrier));
    ctx.spawn(watchPropertyChanged(barrier));

    co_await barrier->wait();

    serviceName = co_await lookupServiceName();

    if (!serviceName.empty())
    {
        ctx.spawn(watchNameOwnerChanged());

        co_await initProperties();
    }

    lg2::info("In Sibling init, sibling alive is {ALIVE}", "ALIVE", alive());

    initialized = true;
}

sdbusplus::async::task<std::string> SiblingImpl::lookupServiceName() const
{
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

    std::vector<std::string> interface{AvailIntf::interface};
    const std::chrono::milliseconds delay{100};
    const size_t retries = 100; // 10 seconds total
    size_t count = 0;
    bool traced = false;

    do
    {
        try
        {
            auto object = co_await ObjectMapper(ctx)
                              .service(ObjectMapper::default_service)
                              .path(ObjectMapper::instance_path)
                              .get_object(objectPath, interface);

            if (object.size() != 1)
            {
                lg2::warning(
                    "Unexpected number of services found for {PATH} = {NUM_SERVICES}",
                    "PATH", objectPath, "NUM_SERVICES", object.size());
            }

            co_return object.begin()->first;
        }
        catch (const sdbusplus::exception_t&)
        {}

        if (!traced)
        {
            traced = true;
            lg2::warning("Sibling service not in mapper, will retry for 10s");
        }
        co_await sdbusplus::async::sleep_for(ctx, delay);

    } while (++count < retries);

    lg2::warning("Could not find sibling service name in mapper");

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

    it = propertyMap.find("FailoverImminent");
    if (it != propertyMap.end())
    {
        auto old = redundancy.failoverImminent;
        redundancy.failoverImminent = std::get<bool>(it->second);
        if (redundancy.failoverImminent != old)
        {
            for (const auto& callback :
                 std::ranges::views::values(foImminentCBs))
            {
                callback(redundancy.failoverImminent);
            }
        }
    }

    it = propertyMap.find("FailoverInProgress");
    if (it != propertyMap.end())
    {
        redundancy.failoverInProgress = std::get<bool>(it->second);
    }

    it = propertyMap.find("Role");
    if (it != propertyMap.end())
    {
        redundancy.role = std::get<Role>(it->second);
    }

    it = propertyMap.find("ReasonsForNoRedundancy");
    if (it != propertyMap.end())
    {
        const auto& reasons =
            std::get<std::vector<ReasonForNoRedundancy>>(it->second);
        redundancy.hasReasonForNoRedundancy = !reasons.empty();
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

    auto old = bmcState.state;
    auto it = propertyMap.find("CurrentBMCState");
    if (it != propertyMap.end())
    {
        bmcState.state = std::get<BMCState>(it->second);
    }

    if (bmcState.state != old)
    {
        for (const auto& callback : std::ranges::views::values(bmcStateCBs))
        {
            callback(bmcState.state);
        }
    }
}

void SiblingImpl::loadAvailabilityProps(
    const SiblingImpl::PropertyMap& propertyMap)
{
    availability.present = true;

    auto it = propertyMap.find("Available");
    if (it != propertyMap.end())
    {
        availability.available = std::get<bool>(it->second);
    }
}

sdbusplus::async::task<> SiblingImpl::watchInterfaceAdded(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(ctx,
                                  rules::interfacesAddedAtPath(objectPath));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_, interfaces] =
            co_await match
                .next<sdbusplus::message::object_path, InterfaceMap>();

        auto prevAlive = alive();

        std::ranges::for_each(interfaces, [this](const auto& entry) {
            loadFromPropertyMap(entry.first, entry.second);
        });

        // If first time seen, wait for the service name to get into
        // the mapper and then start the nameOwnerChanged watch.
        if (serviceName.empty())
        {
            serviceName = co_await lookupServiceName();

            lg2::info("After interfacesAdded, sibling service is {SERVICE}",
                      "SERVICE", serviceName);

            if (!serviceName.empty())
            {
                ctx.spawn(watchNameOwnerChanged());
            }
        }

        // If not alive before and now all interfaces are
        // on D-Bus invoke the callbacks.
        if (!prevAlive && alive())
        {
            for (const auto& callback : std::ranges::views::values(healthCBs))
            {
                callback(true);
            }
        }
    }
}

sdbusplus::async::task<> SiblingImpl::watchInterfaceRemoved(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(ctx,
                                  rules::interfacesRemovedAtPath(objectPath));

    co_await barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_, interfaces] =
            co_await match.next<sdbusplus::message::object_path,
                                std::vector<std::string>>();

        auto prevAlive = alive();

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
        if (std::ranges::contains(interfaces, AvailIntf::interface))
        {
            availability.present = false;
        }

        // If alive before and all interfaces are now gone invoke the callbacks
        if (prevAlive && !alive())
        {
            for (const auto& callback : std::ranges::views::values(healthCBs))
            {
                callback(false);
            }
        }
    }
}

sdbusplus::async::task<> SiblingImpl::watchPropertyChanged(
    std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx, std::format("type='signal',member='PropertiesChanged',path='{}'",
                         objectPath));

    co_await barrier->wait();

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
            lg2::warning("Sibling D-Bus name lost");
            setInterfacesNotPresent();

            // Invoke any health callbacks as sibling is gone.
            for (const auto& callback : std::ranges::views::values(healthCBs))
            {
                callback(false);
            }
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
    else if (interface == AvailIntf::interface)
    {
        loadAvailabilityProps(propertyMap);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::waitForSiblingUp()
{
    using namespace std::chrono_literals;
    auto start = std::chrono::steady_clock::now();
    std::chrono::minutes timeout{6};
    auto waiting = false;

    while (!alive() && ((std::chrono::steady_clock::now() - start) < timeout))
    {
        if (!waiting)
        {
            lg2::info(
                "Waiting up to {TIME} minutes for sibling to become alive",
                "TIME", timeout.count());
            waiting = true;
        }

        co_await sdbusplus::async::sleep_for(ctx, 500ms);
    }

    lg2::info("Done waiting for sibling, alive = {ALIVE}", "ALIVE", alive());
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

    if (!alive() || !noRole())
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
    if (!alive())
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
