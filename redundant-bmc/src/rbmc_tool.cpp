// SPDX-License-Identifier: Apache-2.0

#include "persistent_data.hpp"
#include "redundancy.hpp"
#include "services_impl.hpp"
#include "sibling_reset_impl.hpp"

#include <CLI/CLI.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>
#include <xyz/openbmc_project/Software/Version/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>
#include <xyz/openbmc_project/State/Decorator/Heartbeat/client.hpp>

#include <format>
#include <iostream>

using Redundancy =
    sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;
using BMCState = sdbusplus::client::xyz::openbmc_project::state::BMC<>;
using Role = Redundancy::Role;
using Heartbeat =
    sdbusplus::client::xyz::openbmc_project::state::decorator::Heartbeat<>;
using Version = sdbusplus::client::xyz::openbmc_project::software::Version<>;

constexpr auto siblingService =
    "xyz.openbmc_project.State.BMC.Redundancy.Sibling";

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

void printFONotAllowedReasons()
{
    std::cout << "Reasons failovers are not allowed:\n";
    auto reasons =
        data::read<std::set<std::string>>(data::key::failoversNotAllowedReasons)
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
    auto path =
        sdbusplus::message::object_path{Redundancy::namespace_path::value} /
        Redundancy::namespace_path::bmc;

    std::cout << "Local BMC\n";
    std::cout << "-----------------------------\n";

    try
    {
        auto props = co_await Redundancy(ctx)
                         .service(Redundancy::interface)
                         .path(path.str)
                         .properties();

        auto role = Redundancy::convertRoleToString(props.role);
        // Strip off the sdbusplus prefix to get the final part, e.g. 'Active'.
        role = role.substr(role.find_last_of('.') + 1);
        std::cout << std::format("Role:                {}\n", role);

        rbmc::ServicesImpl services{ctx};
        std::cout << std::format("BMC Position:        {}\n",
                                 services.getBMCPosition());

        std::cout << std::format("Redundancy Enabled:  {}\n",
                                 props.redundancy_enabled);

        if (extended)
        {
            auto bmcState = co_await getBMCState(services);
            std::cout << std::format("BMC State:           {}\n", bmcState);

            std::cout << std::format("Failovers Allowed:   {}\n",
                                     props.failovers_allowed);

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

            if ((role == "Active") && !props.redundancy_enabled)
            {
                printNoRedReasons();
            }

            if ((role == "Active") && props.redundancy_enabled &&
                !props.failovers_allowed)
            {
                printFONotAllowedReasons();
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "Cannot get to Redundancy interface on D-Bus: " << e.what()
                  << "\n";
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> displaySiblingBMCInfo(sdbusplus::async::context& ctx,
                                               bool extended)
{
    auto path =
        sdbusplus::message::object_path{Redundancy::namespace_path::value} /
        Redundancy::namespace_path::sibling_bmc;

    std::cout << "Sibling BMC\n";
    std::cout << "-----------------------------\n";

    try
    {
        auto hbActive = co_await Heartbeat(ctx)
                            .service(siblingService)
                            .path(path.str)
                            .active();
        if (!hbActive)
        {
            std::cout << "No sibling heartbeat\n";
            co_return;
        }

        auto rProps = co_await Redundancy(ctx)
                          .service(siblingService)
                          .path(path.str)
                          .properties();

        auto role = Redundancy::convertRoleToString(rProps.role);
        role = role.substr(role.find_last_of('.') + 1);
        std::cout << std::format("Role:                {}\n", role);

        if (!extended)
        {
            co_return;
        }

        auto fwVersion = co_await Version(ctx)
                             .service(siblingService)
                             .path(path.str)
                             .version();

        auto state = co_await BMCState(ctx)
                         .service(siblingService)
                         .path(path.str)
                         .current_bmc_state();

        auto bmcState = BMCState::convertBMCStateToString(state);
        bmcState = bmcState.substr(bmcState.find_last_of('.') + 1);

        std::cout << std::format("Redundancy Enabled:  {}\n",
                                 rProps.redundancy_enabled);
        std::cout << std::format("Failovers Allowed:   {}\n",
                                 rProps.failovers_allowed);
        std::cout << std::format("BMC State:           {}\n", bmcState);
        std::cout << std::format("FW version hash:     {}\n", fwVersion);
        std::cout << std::format("Provisioned:         {}\n", true); // TODO
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::cout << "Cannot get to a sibling interface on D-Bus: " << e.what()
                  << "\n";
    }
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
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> modifyRedundancyOverride(
    sdbusplus::async::context& ctx, bool disable)
{
    auto path =
        sdbusplus::message::object_path{Redundancy::namespace_path::value} /
        Redundancy::namespace_path::bmc;

    try
    {
        // Use lg2 so it shows up in the journal as coming from rbmctool.
        lg2::info("Setting disable redundancy override to {DISABLED}",
                  "DISABLED", disable);

        co_await Redundancy(ctx)
            .service(Redundancy::interface)
            .path(path.str)
            .disable_redundancy_override(disable);
    }
    catch (const sdbusplus::exception_t& e)
    {
        if (std::string{"xyz.openbmc_project.Common.Error.Unavailable"} ==
            e.name())
        {
            std::cout
                << "Error: Setting cannot be modified now (see journal for details)\n";
        }
        else
        {
            std::cout << "Unexpected error: " << e.what() << '\n';
        }

        exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv)
{
    CLI::App app{"RBMC Tool"};
    bool info{};
    bool extended{};
    bool resetSibling{};
    bool disableRedundancy{};
    bool enableRedundancy{};
    sdbusplus::async::context ctx;

    auto* displayGroup = app.add_option_group("Display RBMC information");
    auto* flag =
        displayGroup->add_flag("-d", info, "Display basic RBMC information");
    displayGroup->add_flag("-e", extended, "Add in extended details")
        ->needs(flag);

    auto* overrideGroup =
        app.add_option_group("Modify the redundancy override");
    auto* disable = overrideGroup->add_flag(
        "-s, --set-disable-redundancy-override", disableRedundancy,
        "Set override to disable redundancy");

    overrideGroup
        ->add_flag("-c, --clear-disable-redundancy-override", enableRedundancy,
                   "Clear override to disable redundancy")
        ->excludes(disable);

    auto* resetGroup = app.add_option_group("Reset sibling BMC");
    resetGroup->add_flag("--reset-sibling", resetSibling,
                         "Reset the sibling BMC");

    app.require_option(1);

    CLI11_PARSE(app, argc, argv);

    if (info)
    {
        ctx.spawn(displayInfo(ctx, extended));
    }
    else if (resetSibling)
    {
        resetSiblingBMC();
    }
    else if (disableRedundancy)
    {
        ctx.spawn(modifyRedundancyOverride(ctx, true));
    }
    else if (enableRedundancy)
    {
        ctx.spawn(modifyRedundancyOverride(ctx, false));
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
