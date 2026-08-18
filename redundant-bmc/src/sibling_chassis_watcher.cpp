// SPDX-License-Identifier: Apache-2.0
#include "sibling_chassis_watcher.hpp"

#include "util.hpp"

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/State/Decorator/Availability/client.hpp>
#include <xyz/openbmc_project/State/Decorator/Availability/common.hpp>

namespace rbmc
{

using Availability =
    sdbusplus::client::xyz::openbmc_project::state::decorator::Availability<>;

namespace rules = sdbusplus::bus::match::rules;

sdbusplus::async::task<> SiblingChassisWatcher::init()
{
    if (!config.checkPassiveBMCChassisAvailable)
    {
        // If checking not necessary, set it to true so it passes
        // the checks when enabling redundancy.
        chassisAvailable = true;
        co_return;
    }

    chassisAvailable = false;

    try
    {
        auto siblingChassisObj = co_await findSiblingChassis();

        if (siblingChassisObj.has_value())
        {
            const auto& [service, path] = siblingChassisObj.value();
            co_await startChassisAvailableWatch(service, path);
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed initializing sibling chassis watch: {ERROR}",
                   "ERROR", e);
    }
}

sdbusplus::async::task<> SiblingChassisWatcher::startChassisAvailableWatch(
    const std::string& service, const std::string& path)
{
    lg2::info("Setting up chassis Available watch on {PATH}", "PATH", path);

    auto barrier = std::make_shared<sdbusplus::async::barrier>(2);

    ctx.spawn(watchChassisAvailablePropertiesChanged(path, barrier));

    barrier->wait();

    try
    {
        chassisAvailable =
            co_await Availability(ctx).service(service).path(path).available();

        lg2::debug("Initial chassis Available is {AVAIL}", "AVAIL",
                   chassisAvailable);
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Failed to read initial chassis Available property: {ERROR}",
                   "ERROR", e);
    }
}

sdbusplus::async::task<>
    SiblingChassisWatcher::watchChassisAvailablePropertiesChanged(
        std::string path, std::shared_ptr<sdbusplus::async::barrier> barrier)
{
    sdbusplus::async::match match(
        ctx, rules::propertiesChanged(path, Availability::interface));

    barrier->wait();

    while (!ctx.stop_requested())
    {
        auto [_, properties] = co_await match.next<
            std::string,
            std::unordered_map<std::string, Availability::PropertiesVariant>>();

        auto it = properties.find("Available");
        if (it != properties.end())
        {
            auto avail = std::get<bool>(it->second);
            if (chassisAvailable != avail)
            {
                chassisAvailable = avail;
                lg2::debug("Sibling chassis Available changed to {AVAIL}",
                           "AVAIL", chassisAvailable);

                services.callSiblingChassisAvailCallbacks(avail);
            }
        }
    }
}

sdbusplus::async::task<std::optional<std::pair<std::string, std::string>>>
    SiblingChassisWatcher::findSiblingChassis()
{
    auto thisPos = services.getBMCPosition();
    if (!thisPos.has_value())
    {
        lg2::warning("Cannot find sibling chassis - this BMC position unknown");
        co_return std::nullopt;
    }

    size_t siblingPos = (thisPos.value() == 0) ? 1 : 0;

    // Get the sibling BMC's parent chassis number from the config file
    auto it = config.bmcConfigs.find(siblingPos);
    if (it == config.bmcConfigs.end())
    {
        lg2::warning(
            "Cannot find sibling chassis - no config for BMC position {POS}",
            "POS", siblingPos);
        co_return std::nullopt;
    }

    if (!it->second.parentChassisNum.has_value())
    {
        lg2::warning(
            "Cannot find sibling chassis - parent_chassis_num not configured for BMC position {POS}",
            "POS", siblingPos);
        co_return std::nullopt;
    }

    co_return co_await util::getChassisObject(
        ctx, it->second.parentChassisNum.value());
}

} // namespace rbmc
