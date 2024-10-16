/* SPDX-License-Identifier: Apache-2.0 */
#include "sibling_impl.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

namespace rbmc
{

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
        // Sibling interface is there, so start the NameOwnerChanged
        // watch and read the initial property values.
        interfacePresent = true;
        ctx.spawn(watchNameOwnerChanged());

        try
        {
            auto sibling = sdbusplus::async::proxy()
                               .service(serviceName)
                               .path(objectPath)
                               .interface(redundancy_ns::Sibling::interface);
            auto props = co_await sibling.get_all_properties<
                redundancy_ns::Sibling::PropertiesVariant>(ctx);

            loadFromPropertyMap(props);
        }
        catch (const sdbusplus::exception_t& e)
        {
            lg2::error("Failed reading sibling properties: {ERROR}", "ERROR",
                       e);
            interfacePresent = false;
        }
    }
    else
    {
        // Sibling interface not on D-Bus.
        interfacePresent = false;
    }

    lg2::info("In Sibling init, interface present is {PRESENT}", "PRESENT",
              interfacePresent);

    initialized = true;
}

// NOLINTNEXTLINE
sdbusplus::async::task<std::string> SiblingImpl::getServiceName()
{
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

    try
    {
        auto mapper = ObjectMapper(ctx)
                          .service(ObjectMapper::default_service)
                          .path(ObjectMapper::instance_path);

        std::vector<std::string> interface{redundancy_ns::Sibling::interface};
        auto object = co_await mapper.get_object(objectPath, interface);
        co_return object.begin()->first;
    }
    catch (const sdbusplus::exception_t&)
    {}

    co_return std::string{};
}

// namespace util

void SiblingImpl::loadFromPropertyMap(
    const SiblingImpl::PropertyMap& propertyMap)
{
    auto it = propertyMap.find("BMCPosition");
    if (it != propertyMap.end())
    {
        bmcPosition = std::get<size_t>(it->second);
    }

    it = propertyMap.find("FWVersion");
    if (it != propertyMap.end())
    {
        fwVersion = std::get<std::string>(it->second);
    }

    it = propertyMap.find("Provisioned");
    if (it != propertyMap.end())
    {
        provisioned = std::get<bool>(it->second);
    }

    it = propertyMap.find("RedundancyEnabled");
    if (it != propertyMap.end())
    {
        redEnabled = std::get<bool>(it->second);
    }

    it = propertyMap.find("FailoversPaused");
    if (it != propertyMap.end())
    {
        failoversPaused = std::get<bool>(it->second);
    }

    it = propertyMap.find("BMCState");
    if (it != propertyMap.end())
    {
        bmcState = std::get<BMCState>(it->second);
    }

    it = propertyMap.find("Role");
    if (it != propertyMap.end())
    {
        role = std::get<Role>(it->second);
    }

    it = propertyMap.find("CommunicationOK");
    if (it != propertyMap.end())
    {
        commsOK = std::get<bool>(it->second);
    }

    it = propertyMap.find("Heartbeat");
    if (it != propertyMap.end())
    {
        heartbeat = std::get<bool>(it->second);
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

        auto it = interfaces.find(redundancy_ns::Sibling::interface);
        if (it != interfaces.end())
        {
            lg2::info("Sibling D-Bus interface added");
            loadFromPropertyMap(it->second);
            interfacePresent = true;

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
                    lg2::info(
                        "After interfacesAdded, sibling service is {SERVICE}",
                        "SERVICE", serviceName);
                } while (serviceName.empty() && (count++ < mapperRetries));

                if (!serviceName.empty())
                {
                    ctx.spawn(watchNameOwnerChanged());
                }
            }
        }
    }
    co_return;
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

        if (std::ranges::contains(interfaces,
                                  redundancy_ns::Sibling::interface))
        {
            lg2::info("Sibling D-Bus interface removed");
            interfacePresent = false;
            heartbeat = false;
        }
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> SiblingImpl::watchPropertyChanged()
{
    namespace rules = sdbusplus::bus::match::rules;
    sdbusplus::async::match match(
        ctx, rules::propertiesChanged(objectPath,
                                      redundancy_ns::Sibling::interface));

    while (!ctx.stop_requested())
    {
        auto [iface,
              propertyMap] = co_await match.next<std::string, PropertyMap>();

        for (const auto& [name, value] : propertyMap)
        {
            lg2::info("Sibling property {PROP} changed", "PROP", name);
        }

        loadFromPropertyMap(propertyMap);
    }

    co_return;
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
            interfacePresent = false;
            heartbeat = false;
        }
    }
    co_return;
}

// NOLINTBEGIN
sdbusplus::async::task<>
    SiblingImpl::waitForSiblingUp(const std::chrono::seconds& timeout)
// NOLINTEND
{
    using namespace std::chrono_literals;
    auto start = std::chrono::steady_clock::now();
    auto waiting = false;

    while ((!interfacePresent || !heartbeat) &&
           ((std::chrono::steady_clock::now() - start) < timeout))
    {
        if (!waiting)
        {
            lg2::info("Waiting for sibling interface and/or heartbeat: "
                      "Present = {PRES}, Heartbeat = {HB}",
                      "PRES", interfacePresent, "HB", heartbeat);
            waiting = true;
        }

        co_await sdbusplus::async::sleep_for(ctx, 500ms);
    }

    lg2::info(
        "Done waiting for sibling. Interface present = {PRES}, heartbeat = {HB}",
        "PRES", interfacePresent, "HB", heartbeat);

    co_return;
}

} // namespace rbmc
