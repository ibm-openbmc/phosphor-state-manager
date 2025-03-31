// SPDX-License-Identifier: Apache-2.0

#include "persistent_data.hpp"
#include "redundancy.hpp"
#include "services_impl.hpp"
#include "sibling_reset_impl.hpp"

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
sdbusplus::async::task<std::string> getBMCState(const rbmc::Services& services)
{
    try
    {
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        auto bmcState = co_await services.getBMCState();

        auto stateString = BMCState::convertBMCStateToString(bmcState);
        co_return stateString.substr(stateString.find_last_of('.') + 1);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Just show the error in the state field
        co_return e.what();
    }
}

void printNoRedReasons()
{
    using NoRedDetails =
        std::map<rbmc::redundancy::NoRedundancyReason, std::string>;
    auto details = data::read<NoRedDetails>(data::key::noRedDetails)
                       .value_or(NoRedDetails{});
    std::cout << std::format("Reasons for no BMC redundancy:\n");
    if (!details.empty())
    {
        for (const auto& d : std::views::values(details))
        {
            std::cout << std::format("    {}\n", d);
        }
    }
    else
    {
        // There can be long periods where the active BMC is waiting
        // for the passive BMC so redundancy can't be checked yet.
        // As far as rbmctool goes, label them as in a transition.
        std::cout << std::format("    In transition\n");
    }
}

void printFOPReasons()
{
    std::cout << "Reasons for failovers paused:\n";
    auto reasons =
        data::read<std::set<std::string>>(data::key::failoversPausedReasons)
            .value_or(std::set<std::string>());
    if (!reasons.empty())
    {
        std::ranges::for_each(reasons, [](auto& reason) {
            std::cout << std::format("    {}\n", reason);
        });
    }
    else
    {
        std::cout << std::format("    Unknown\n");
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
            auto bmcState = co_await getBMCState(services);
            std::cout << std::format("BMC State:           {}\n", bmcState);

            auto paused = std::get<bool>(props.at("FailoversPaused"));
            std::cout << std::format("Failovers Paused:    {}\n", paused);

            std::cout << std::format("FW version hash:     {}\n",
                                     services.getFWVersion());
            std::cout << std::format("Provisioned:         {}\n",
                                     services.getProvisioned());
            if (role != "Unknown")
            {
                std::cout << std::format(
                    "Role Reason:         {}\n",
                    data::read<std::string>(data::key::roleReason)
                        .value_or("No reason found"));
            }

            if ((role == "Active") && !enabled)
            {
                printNoRedReasons();
            }

            if ((role == "Active") && enabled && paused)
            {
                printFOPReasons();
            }
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

void resetSiblingBMC()
{
    rbmc::SiblingResetImpl reset;

    try
    {
        reset.assertReset();
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed asserting sibling reset: {ERROR}", "ERROR", e);
        return;
    }

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);

    try
    {
        reset.releaseReset();
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed releasing sibling reset: {ERROR}", "ERROR", e);
    }
}

int main(int argc, char** argv)
{
    CLI::App app{"RBMC Tool"};
    bool info{};
    bool extended{};
    bool resetSibling{};
    sdbusplus::async::context ctx;

    auto* flag = app.add_flag("-d", info, "Display RBMC Information");
    app.add_flag("-e", extended, "Add extended RBMC details to the display")
        ->needs(flag);

    app.add_flag("--reset-sibling", resetSibling, "Reset the sibling BMC");

    CLI11_PARSE(app, argc, argv);

    if (info)
    {
        ctx.spawn(displayInfo(ctx, extended));
    }
    else if (resetSibling)
    {
        resetSiblingBMC();
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
