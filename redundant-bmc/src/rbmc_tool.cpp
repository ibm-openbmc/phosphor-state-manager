// SPDX-License-Identifier: Apache-2.0

#include "services_impl.hpp"

#include <CLI/CLI.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/Sibling/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>

#include <format>
#include <iostream>

using Sibling =
    sdbusplus::client::xyz::openbmc_project::state::bmc::redundancy::Sibling<>;
using Redundancy =
    sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;
using BMCState = sdbusplus::client::xyz::openbmc_project::state::BMC<>;
using Role = Redundancy::Role;

// NOLINTNEXTLINE
sdbusplus::async::task<std::string> getBMCState(sdbusplus::async::context& ctx)
{
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;
    using StateMgr = sdbusplus::client::xyz::openbmc_project::state::BMC<>;

    try
    {
        auto mapper = ObjectMapper(ctx)
                          .service(ObjectMapper::default_service)
                          .path(ObjectMapper::instance_path);

        std::string statePath = std::string{BMCState::namespace_path::value} +
                                '/' + BMCState::namespace_path::bmc;
        std::vector<std::string> stateIface{BMCState::interface};

        auto object = co_await mapper.get_object(statePath, stateIface);
        auto service = object.begin()->first;

        auto stateMgr = StateMgr(ctx).service(service).path(statePath);
        auto bmcState = co_await stateMgr.current_bmc_state();

        auto stateString = BMCState::convertBMCStateToString(bmcState);
        co_return stateString.substr(stateString.find_last_of('.') + 1);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Just show the error in the state field
        co_return e.what();
    }
}

// NOLINTBEGIN
sdbusplus::async::task<> displayLocalBMCInfo(sdbusplus::async::context& ctx,
                                             bool extended)
// NOLINTEND
{
    auto redundancy = sdbusplus::async::proxy()
                          .service("xyz.openbmc_project.State.BMC.Redundancy")
                          .path(Redundancy::instance_path)
                          .interface(Redundancy::interface);

    std::cout << "Local BMC\n";
    std::cout << "-----------------------------\n";

    try
    {
        auto props =
            co_await redundancy
                .get_all_properties<Redundancy::PropertiesVariant>(ctx);

        auto role =
            Redundancy::convertRoleToString(std::get<Role>(props.at("Role")));
        // Strip off the sdbusplus prefix to get the final part, e.g. 'Active'.
        role = role.substr(role.find_last_of('.') + 1);
        std::cout << std::format("Role:                {}\n", role);

        rbmc::ServicesImpl services{ctx};
        std::cout << std::format("BMC Position:        {}\n",
                                 services.getBMCPosition());

        auto enabled = std::get<bool>(props.at("RedundancyEnabled"));
        std::cout << std::format("Redundancy Enabled:  {}\n", enabled);

        if (extended)
        {
            auto bmcState = co_await getBMCState(ctx);
            std::cout << std::format("BMC State:           {}\n", bmcState);

            std::cout << std::format("Failovers Paused:    {}\n",
                                     false); // TODO
            std::cout << std::format("FW version hash:     {}\n",
                                     services.getFWVersion());
            std::cout << std::format("Provisioned:         {}\n",
                                     services.getProvisioned());
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "Cannot get to Redundancy interface on D-Bus: " << e.what()
                  << "\n";
    }

    co_return;
}

// NOLINTBEGIN
sdbusplus::async::task<> displaySiblingBMCInfo(sdbusplus::async::context& ctx,
                                               bool extended)
// NOLINTEND
{
    std::string path = std::string{Sibling::namespace_path::value} + '/' +
                       Sibling::namespace_path::bmc;
    auto sibling =
        sdbusplus::async::proxy()
            .service("xyz.openbmc_project.State.BMC.Redundancy.Sibling")
            .path(path)
            .interface(Sibling::interface);

    std::cout << "Sibling BMC\n";
    std::cout << "-----------------------------\n";

    try
    {
        auto props =
            co_await sibling.get_all_properties<Sibling::PropertiesVariant>(
                ctx);

        if (!std::get<bool>(props.at("Heartbeat")))
        {
            std::cout << "No sibling heartbeat\n";
            co_return;
        }

        auto role =
            Redundancy::convertRoleToString(std::get<Role>(props.at("Role")));
        role = role.substr(role.find_last_of('.') + 1);
        std::cout << std::format("Role:                {}\n", role);

        if (!extended)
        {
            co_return;
        }

        auto bmcState = BMCState::convertBMCStateToString(
            std::get<BMCState::BMCState>(props.at("BMCState")));
        bmcState = bmcState.substr(bmcState.find_last_of('.') + 1);
        auto pos = std::get<size_t>(props.at("BMCPosition"));
        auto provisioned = std::get<bool>(props.at("Provisioned"));
        auto commOK = std::get<bool>(props.at("CommunicationOK"));
        auto fwVersion = std::get<std::string>(props.at("FWVersion"));
        auto paused = std::get<bool>(props.at("FailoversPaused"));
        auto enabled = std::get<bool>(props.at("RedundancyEnabled"));

        std::cout << std::format("BMC Position:        {}\n", pos);
        std::cout << std::format("BMC State:           {}\n", bmcState);
        std::cout << std::format("Redundancy Enabled:  {}\n", enabled);
        std::cout << std::format("Failovers Paused:    {}\n", paused);
        std::cout << std::format("Sibling Comm OK:     {}\n", commOK);
        std::cout << std::format("FW version hash:     {}\n", fwVersion);
        std::cout << std::format("Provisioned:         {}\n", provisioned);
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cout << "Cannot get to Sibling interface on D-Bus: " << e.what()
                  << "\n";
    }

    co_return;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> displayInfo(sdbusplus::async::context& ctx,
                                     bool extended)
{
    std::cout << "\n";
    co_await displayLocalBMCInfo(ctx, extended);
    std::cout << "\n";
    co_await displaySiblingBMCInfo(ctx, extended);
    std::cout << "\n";
    co_return;
}

int main(int argc, char** argv)
{
    CLI::App app{"RBMC Tool"};
    bool info{};
    bool extended{};
    sdbusplus::async::context ctx;

    auto* flag = app.add_flag("-d", info, "Display RBMC Information");
    app.add_flag("-e", extended, "Add extended RBMC details to the display")
        ->needs(flag);

    CLI11_PARSE(app, argc, argv);

    if (info)
    {
        ctx.spawn(displayInfo(ctx, extended));
    }
    else
    {
        std::cout << app.help();
    }

    ctx.spawn(
        sdbusplus::async::execution::just() |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    return 0;
}
