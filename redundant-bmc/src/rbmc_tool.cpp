// SPDX-License-Identifier: Apache-2.0

#include "persistent_data.hpp"
#include "redundancy.hpp"
#include "services_impl.hpp"
#include "sibling_reset_impl.hpp"

#include <CLI/CLI.hpp>
#include <xyz/openbmc_project/Control/Failover/client.hpp>
#include <xyz/openbmc_project/Software/Version/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>
#include <xyz/openbmc_project/State/Decorator/Heartbeat/client.hpp>

#include <format>
#include <print>

using Redundancy =
    sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;
using BMCState = sdbusplus::client::xyz::openbmc_project::state::BMC<>;
using Role = Redundancy::Role;
using Failover = sdbusplus::client::xyz::openbmc_project::control::Failover<>;
using Heartbeat =
    sdbusplus::client::xyz::openbmc_project::state::decorator::Heartbeat<>;
using Version = sdbusplus::client::xyz::openbmc_project::software::Version<>;

constexpr auto siblingService =
    "xyz.openbmc_project.State.BMC.Redundancy.Sibling";

template <typename T>
void printParam(std::string_view key, const T& value)
{
    std::println("{:22}{}", key, value);
}

void printReason(std::string_view reason)
{
    std::println("    {}", reason);
}

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
    std::println("Reasons for no BMC redundancy:");
    if (!details.empty())
    {
        for (const auto& d : std::views::values(details))
        {
            printReason(d);
        }
    }
    else
    {
        // There can be long periods where the active BMC is waiting
        // for the passive BMC so redundancy can't be checked yet.
        // As far as rbmctool goes, label them as in a transition.
        printReason("In transition");
    }
}

void printFONotAllowedReasons()
{
    std::println("Reasons failovers are not allowed:");
    auto reasons =
        data::read<std::set<std::string>>(data::key::failoversNotAllowedReasons)
            .value_or(std::set<std::string>());
    if (!reasons.empty())
    {
        std::ranges::for_each(reasons, [](auto& reason) {
            printReason(reason);
        });
    }
    else
    {
        printReason("Unknown");
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

    std::println("Local BMC");
    std::println("-----------------------------");

    try
    {
        auto props = co_await Redundancy(ctx)
                         .service(Redundancy::interface)
                         .path(path.str)
                         .properties();

        auto role = Redundancy::convertRoleToString(props.role);
        // Strip off the sdbusplus prefix to get the final part, e.g. 'Active'.
        role = role.substr(role.find_last_of('.') + 1);
        printParam("Role:", role);

        rbmc::ServicesImpl services{ctx};
        printParam("BMC Position:", services.getBMCPosition());

        printParam("Redundancy Enabled:", props.redundancy_enabled);

        if (extended)
        {
            auto bmcState = co_await getBMCState(services);
            printParam("BMC State:", bmcState);
            printParam("Failovers Allowed:", props.failovers_allowed);
            printParam("Failover In Progress:", props.failover_in_progress);
            printParam("FW Version Hash:", services.getFWVersion());
            printParam("Provisioned:", services.getProvisioned());

            if (role != "Unknown")
            {
                printParam("Role Reason:",
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
        std::println("Cannot get to Redundancy interface on D-Bus: {}",
                     e.what());
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> displaySiblingBMCInfo(sdbusplus::async::context& ctx,
                                               bool extended)
{
    auto path =
        sdbusplus::message::object_path{Redundancy::namespace_path::value} /
        Redundancy::namespace_path::sibling_bmc;

    std::println("Sibling BMC");
    std::println("-----------------------------");

    try
    {
        auto hbActive = co_await Heartbeat(ctx)
                            .service(siblingService)
                            .path(path.str)
                            .active();
        if (!hbActive)
        {
            std::println("No sibling heartbeat");
            co_return;
        }

        auto rProps = co_await Redundancy(ctx)
                          .service(siblingService)
                          .path(path.str)
                          .properties();

        auto role = Redundancy::convertRoleToString(rProps.role);
        role = role.substr(role.find_last_of('.') + 1);
        printParam("Role:", role);

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

        printParam("Redundancy Enabled:", rProps.redundancy_enabled);
        printParam("Failovers Allowed:", rProps.failovers_allowed);
        printParam("BMC State:", bmcState);
        printParam("FW Version Hash:", fwVersion);
        printParam("Provisioned:", true); // TODO
    }
    catch (const sdbusplus::exception_t& e)
    {
        std::println("Cannot get to a sibling interface on D-Bus: {}",
                     e.what());
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> displayInfo(sdbusplus::async::context& ctx,
                                     bool extended)
{
    std::println();
    co_await displayLocalBMCInfo(ctx, extended);
    std::println();
    co_await displaySiblingBMCInfo(ctx, extended);
    std::println();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> resetSiblingBMC(sdbusplus::async::context& ctx)
{
    rbmc::SiblingResetImpl reset{ctx};

    try
    {
        co_await reset.toggleReset();
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed asserting sibling reset: {ERROR}", "ERROR", e);
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
            std::println(
                "Error: Setting cannot be modified now (see journal for details)");
        }
        else
        {
            std::println("Unexpected error: {}", e.what());
        }

        exit(EXIT_FAILURE);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> startFailover(sdbusplus::async::context& ctx,
                                       bool force)
{
    using FailoverOptions = std::map<std::string, std::variant<bool>>;
    FailoverOptions options;

    try
    {
        if (force)
        {
            lg2::info("Initiating forced failover");
            options.emplace(
                Failover::convertOptionsToString(Failover::Options::Force),
                force);
        }
        else
        {
            lg2::info("Initiating failover");
        }

        auto path =
            sdbusplus::message::object_path{Redundancy::namespace_path::value} /
            Redundancy::namespace_path::bmc;

        co_await Failover(ctx)
            .service(Redundancy::interface)
            .path(path.str)
            .start_failover(options);
    }
    catch (const sdbusplus::exception_t& e)
    {
        if (std::string{"xyz.openbmc_project.Common.Error.Unavailable"} ==
            e.name())
        {
            std::println(
                "Error: Failover cannot be started now (see journal for details)");
        }
        else
        {
            std::println("Unexpected error: {}", e.what());
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
    bool failover{};
    bool forceFailover{};
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

    auto* failoverGroup = app.add_option_group("Starting failovers");
    auto* fo =
        failoverGroup->add_flag("-f, --failover", failover, "Start a failover");
    failoverGroup
        ->add_flag("-r, --force-failover", forceFailover,
                   "Start a forced failover. Only for emergencies.")
        ->excludes(fo);

    app.require_option(1);

    CLI11_PARSE(app, argc, argv);

    if (info)
    {
        ctx.spawn(displayInfo(ctx, extended));
    }
    else if (resetSibling)
    {
        ctx.spawn(resetSiblingBMC(ctx));
    }
    else if (disableRedundancy)
    {
        ctx.spawn(modifyRedundancyOverride(ctx, true));
    }
    else if (enableRedundancy)
    {
        ctx.spawn(modifyRedundancyOverride(ctx, false));
    }
    else if (failover || forceFailover)
    {
        ctx.spawn(startFailover(ctx, forceFailover));
    }
    else
    {
        std::println("{}", app.help());
    }

    ctx.spawn(
        sdbusplus::async::execution::just() |
        sdbusplus::async::execution::then([&ctx]() { ctx.request_stop(); }));
    ctx.run();

    return 0;
}
