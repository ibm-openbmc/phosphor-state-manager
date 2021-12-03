#include "config.h"

#include "host_state_manager.hpp"
#include "settings.hpp"
#include "xyz/openbmc_project/Common/error.hpp"
#include "xyz/openbmc_project/Control/Power/RestorePolicy/server.hpp"

#include <getopt.h>
#include <systemd/sd-bus.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/server.hpp>

#include <iostream>
#include <map>
#include <string>

namespace phosphor
{
namespace state
{
namespace manager
{

PHOSPHOR_LOG2_USING;

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace sdbusplus::xyz::openbmc_project::Control::Power::server;

constexpr auto MAPPER_BUSNAME = "xyz.openbmc_project.ObjectMapper";
constexpr auto MAPPER_PATH = "/xyz/openbmc_project/object_mapper";
constexpr auto MAPPER_INTERFACE = "xyz.openbmc_project.ObjectMapper";

constexpr auto PROPERTY_INTERFACE = "org.freedesktop.DBus.Properties";

std::string getService(sdbusplus::bus::bus& bus, std::string path,
                       std::string interface)
{
    auto mapper = bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                      MAPPER_INTERFACE, "GetObject");

    mapper.append(path, std::vector<std::string>({interface}));

    std::map<std::string, std::vector<std::string>> mapperResponse;
    try
    {
        auto mapperResponseMsg = bus.call(mapper);

        mapperResponseMsg.read(mapperResponse);
        if (mapperResponse.empty())
        {
            error("Mapper response empty, does not have path {PATH} and "
                  "interface {INTERFACE}",
                  "PATH", path, "INTERFACE", interface);
            throw std::runtime_error("Error reading mapper response");
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error in mapper call for path {PATH} and interface {INTERFACE} "
              "with error {ERROR}",
              "PATH", path, "INTERFACE", interface, "ERROR", e);
        throw;
    }

    return mapperResponse.begin()->first;
}

std::string getProperty(sdbusplus::bus::bus& bus, std::string path,
                        std::string interface, std::string propertyName)
{
    std::variant<std::string> property;
    std::string service = getService(bus, path, interface);

    auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                      PROPERTY_INTERFACE, "Get");

    method.append(interface, propertyName);

    try
    {
        auto reply = bus.call(method);
        reply.read(property);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error in property Get, error {ERROR}, property {PROPERTY}",
              "ERROR", e, "PROPERTY", propertyName);
        throw;
    }

    if (std::get<std::string>(property).empty())
    {
        error("Error reading property response for {PROPERTY}", "PROPERTY",
              propertyName);
        throw std::runtime_error("Error reading property response");
    }

    return std::get<std::string>(property);
}

void setProperty(sdbusplus::bus::bus& bus, const std::string& path,
                 const std::string& interface, const std::string& property,
                 const std::string& value)
{
    std::variant<std::string> variantValue = value;
    std::string service = getService(bus, path, interface);

    auto method = bus.new_method_call(service.c_str(), path.c_str(),
                                      PROPERTY_INTERFACE, "Set");

    method.append(interface, property, variantValue);
    bus.call_noreply(method);

    return;
}

} // namespace manager
} // namespace state
} // namespace phosphor

int main(int argc, char** argv)
{
    using namespace phosphor::logging;

    std::string hostPath = "/xyz/openbmc_project/state/host0";
    int arg;
    int optIndex = 0;

    static struct option longOpts[] = {{"host", required_argument, 0, 'h'},
                                       {0, 0, 0, 0}};

    while ((arg = getopt_long(argc, argv, "h:", longOpts, &optIndex)) != -1)
    {
        switch (arg)
        {
            case 'h':
                hostPath =
                    std::string("/xyz/openbmc_project/state/host") + optarg;
                break;
            default:
                break;
        }
    }

    auto bus = sdbusplus::bus::new_default();

    using namespace settings;
    Objects settings(bus);

    using namespace phosphor::state::manager;
    namespace server = sdbusplus::xyz::openbmc_project::State::server;

    // This application is only run if chassis power is off

    /* The logic here is to first check the one-time PowerRestorePolicy setting.
     * If this property is not the default then look at the persistent
     * user setting in the non one-time object, otherwise honor the one-time
     * setting.
     */
    auto methodOneTime = bus.new_method_call(
        settings.service(settings.powerRestorePolicy, powerRestoreIntf).c_str(),
        settings.powerRestorePolicyOneTime.c_str(),
        "org.freedesktop.DBus.Properties", "Get");
    methodOneTime.append(powerRestoreIntf, "PowerRestorePolicy");

    auto methodUserSetting = bus.new_method_call(
        settings.service(settings.powerRestorePolicy, powerRestoreIntf).c_str(),
        settings.powerRestorePolicy.c_str(), "org.freedesktop.DBus.Properties",
        "Get");
    methodUserSetting.append(powerRestoreIntf, "PowerRestorePolicy");

    std::variant<std::string> result;
    try
    {
        auto reply = bus.call(methodOneTime);
        reply.read(result);
        auto powerPolicy = std::get<std::string>(result);

        if (RestorePolicy::Policy::None ==
            RestorePolicy::convertPolicyFromString(powerPolicy))
        {
            // one_time is set to None so use the customer setting
            info("One time not set, check user setting of power policy");
            auto reply = bus.call(methodUserSetting);
            reply.read(result);
            powerPolicy = std::get<std::string>(result);
        }
        else
        {
            // one_time setting was set so we're going to use it. Reset it
            // to default for next time.
            info("One time set, use it and reset to default");
            setProperty(bus, settings.powerRestorePolicyOneTime.c_str(),
                        powerRestoreIntf, "PowerRestorePolicy",
                        convertForMessage(RestorePolicy::Policy::None));
        }

        info("Host power is off, processing power policy {POWER_POLICY}",
             "POWER_POLICY", powerPolicy);

        if (RestorePolicy::Policy::AlwaysOn ==
            RestorePolicy::convertPolicyFromString(powerPolicy))
        {
            info("power_policy=ALWAYS_POWER_ON, powering host on");
            setProperty(bus, hostPath, HOST_BUSNAME, "RestartCause",
                        convertForMessage(
                            server::Host::RestartCause::PowerPolicyAlwaysOn));
            setProperty(bus, hostPath, HOST_BUSNAME, "RequestedHostTransition",
                        convertForMessage(server::Host::Transition::On));
        }
        else if (RestorePolicy::Policy::Restore ==
                 RestorePolicy::convertPolicyFromString(powerPolicy))
        {
            info("power_policy=RESTORE, restoring last state");
            setProperty(
                bus, hostPath, HOST_BUSNAME, "RestartCause",
                convertForMessage(
                    server::Host::RestartCause::PowerPolicyPreviousState));
            // Read last requested state and re-request it to execute it
            auto hostReqState = getProperty(bus, hostPath, HOST_BUSNAME,
                                            "RequestedHostTransition");
            setProperty(bus, hostPath, HOST_BUSNAME, "RequestedHostTransition",
                        hostReqState);
        }
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error in PowerRestorePolicy Get: {ERROR}", "ERROR", e);
        elog<InternalFailure>();
    }

    return 0;
}
