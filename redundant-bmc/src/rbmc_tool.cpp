// SPDX-License-Identifier: Apache-2.0

#include "persistent_data.hpp"
#include "redundancy.hpp"
#include "services_impl.hpp"
#include "sibling_reset_impl.hpp"
#include "types.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/Control/Failover/client.hpp>
#include <xyz/openbmc_project/Software/Version/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>

#include <format>
#include <print>

using Redundancy =
    sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;
using BMCState = sdbusplus::client::xyz::openbmc_project::state::BMC<>;
using Role = Redundancy::Role;
using Failover = sdbusplus::client::xyz::openbmc_project::control::Failover<>;
using Version = sdbusplus::client::xyz::openbmc_project::software::Version<>;

constexpr auto siblingService =
    "xyz.openbmc_project.State.BMC.Redundancy.Sibling";

template <typename T>
void printParam(std::string key, const T& value)
{
    key.push_back(':');
    std::println("{:22}{}", key, value);
}

void printReason(std::string_view reason)
{
    std::println("    {}", reason);
}

void printJSONParam(const std::string& name, const nlohmann::json& value)
{
    if (value.is_boolean())
    {
        printParam(name, value.get<bool>());
    }
    else if (value.is_string())
    {
        printParam(name, value.get<std::string>());
    }
    else if (value.is_number_integer())
    {
        printParam(name, value.get<int>());
    }
    else if (value.is_object())
    {
        std::println("{}:", name);
        for (const auto& [subName, subVal] : value.items())
        {
            // Only need string support here and they look
            // better not using dump. But keep dump() just in case.
            std::string printVal =
                subVal.is_string() ? subVal.get<std::string>() : subVal.dump();
            printReason(std::format("{}: {}", subName, printVal));
        }
    }
    else if (value.is_array())
    {
        std::println("{}:", name);
        for (const auto& v : value)
        {
            // Same string support statement as above.
            std::string printVal =
                (v.is_string()) ? v.get<std::string>() : v.dump();
            printReason(printVal);
        }
    }
    else
    {
        printParam(name, value.dump());
    }
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

void addNoRedReasons(const rbmc::redundancy::ReasonsForNoRedundancy& reasons,
                     nlohmann::ordered_json& output)
{
    nlohmann::json::array_t reasonList;
    if (!reasons.empty())
    {
        std::ranges::for_each(reasons, [&reasonList](const auto& r) {
            auto shortName =
                Redundancy::convertReasonForNoRedundancyToString(r);
            shortName = shortName.substr(shortName.find_last_of('.') + 1);
            reasonList.push_back(shortName);
        });
    }
    else
    {
        // There can be long periods where the active BMC is waiting
        // for the passive BMC so redundancy can't be checked yet.
        // As far as rbmctool goes, label them as in a transition.
        reasonList.emplace_back("In transition");
    }

    output["Reasons for no BMC redundancy"] = std::move(reasonList);
}

void addFONotAllowedReasons(nlohmann::ordered_json& output)
{
    nlohmann::json::array_t reasonList;
    auto reasons =
        data::read<std::set<std::string>>(data::key::failoversNotAllowedReasons)
            .value_or(std::set<std::string>());
    if (!reasons.empty())
    {
        std::ranges::for_each(reasons, [&reasonList](auto& reason) {
            reasonList.push_back(reason);
        });
    }
    else
    {
        reasonList.emplace_back("Unknown");
    }

    output["Reasons failovers are not allowed"] = std::move(reasonList);
}

void addLastFailoverDetails(const std::filesystem::path& dataPath,
                            size_t bmcPos, nlohmann::ordered_json& output)
{
    auto logs = data::getFailoverLogs(dataPath, bmcPos);

    if (!logs.empty())
    {
        auto& object = output["Last failover driven by this BMC"];
        const auto& last = logs.back();
        object["Requester"] = last.first;
        object["Timestamp"] = last.second;
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> getLocalBMCInfo(sdbusplus::async::context& ctx,
                                         bool extended,
                                         nlohmann::ordered_json& output)
{
    auto path =
        sdbusplus::message::object_path{Redundancy::namespace_path::value} /
        Redundancy::namespace_path::bmc;

    try
    {
        auto props = co_await Redundancy(ctx)
                         .service(Redundancy::interface)
                         .path(path.str)
                         .properties();

        auto role = Redundancy::convertRoleToString(props.role);
        // Strip off the sdbusplus prefix to get the final part, e.g. 'Active'.
        role = role.substr(role.find_last_of('.') + 1);
        output["Role"] = role;

        rbmc::ServicesImpl services{ctx};
        auto pos = services.getBMCPosition();
        auto bmcPos = pos.has_value() ? std::to_string(pos.value()) : "Unknown";
        output["BMC Position"] = bmcPos;

        output["Redundancy Enabled"] = props.redundancy_enabled;

        if (!extended)
        {
            co_return;
        }

        auto bmcState = co_await getBMCState(services);
        output["BMC State"] = bmcState;
        output["Failovers Allowed"] = props.failovers_allowed;
        output["Failover In Progress"] = props.failover_in_progress;
        output["FW Version Hash"] = services.getFWVersion();
        output["Provisioned"] = services.getProvisioned();

        if (role != "Unknown")
        {
            output["Role Reason"] =
                data::read<std::string>(data::key::roleReason)
                    .value_or("No reason found");
        }

        if ((role == "Active") && !props.redundancy_enabled)
        {
            addNoRedReasons(props.reasons_for_no_redundancy, output);
        }
        else if (role == "Passive")
        {
            output["Cannot Be Active"] =
                !props.reasons_for_no_redundancy.empty();
        }

        if ((role == "Active") && props.redundancy_enabled &&
            !props.failovers_allowed)
        {
            addFONotAllowedReasons(output);
        }

        if ((role == "Active") && pos.has_value())
        {
            addLastFailoverDetails(services.getPersistentDataPath(),
                                   pos.value(), output);
        }
    }
    catch (const std::exception& e)
    {
        // Don't print an error so we don't corrupt the JSON output
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> getSiblingBMCInfo(sdbusplus::async::context& ctx,
                                           bool extended,
                                           nlohmann::ordered_json& output)
{
    auto path =
        sdbusplus::message::object_path{Redundancy::namespace_path::value} /
        Redundancy::namespace_path::sibling_bmc;

    try
    {
        auto rProps = co_await Redundancy(ctx)
                          .service(siblingService)
                          .path(path.str)
                          .properties();

        auto role = Redundancy::convertRoleToString(rProps.role);
        role = role.substr(role.find_last_of('.') + 1);
        output["Role"] = role;

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

        output["Redundancy Enabled"] = rProps.redundancy_enabled;
        output["Failovers Allowed"] = rProps.failovers_allowed;
        output["BMC State"] = bmcState;
        output["FW Version Hash"] = fwVersion;
        output["Provisioned"] = true; // TODO
    }
    catch (const std::exception& e)
    {
        // Don't print an error so we don't corrupt the JSON output
    }
}

void displayBMCInfo(const nlohmann::ordered_json& bmcInfo)
{
    for (const auto& [name, value] : bmcInfo.items())
    {
        printJSONParam(name, value);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<> displayInfo(sdbusplus::async::context& ctx,
                                     bool extended, bool jsonOutput)
{
    nlohmann::ordered_json localInfo;
    nlohmann::ordered_json siblingInfo;
    co_await getLocalBMCInfo(ctx, extended, localInfo);
    co_await getSiblingBMCInfo(ctx, extended, siblingInfo);

    if (jsonOutput)
    {
        nlohmann::ordered_json out;
        out["Local BMC"] = std::move(localInfo);
        out["Sibling BMC"] = std::move(siblingInfo);
        std::println("{}", out.dump(4));
    }
    else
    {
        std::println();
        std::println("Local BMC");
        std::println("-----------------------------");

        if (!localInfo.empty())
        {
            displayBMCInfo(localInfo);
        }
        else
        {
            std::println("Local BMC data not available");
        }

        std::println();
        std::println("Sibling BMC");
        std::println("-----------------------------");

        if (!siblingInfo.empty())
        {
            displayBMCInfo(siblingInfo);
        }
        else
        {
            std::println("Sibling BMC data not available");
        }

        std::println();
    }
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
    rbmc::FailoverOptions options;

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
            .start_failover(Failover::Requester::Tool, options);
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
    bool jsonOutput{};
    sdbusplus::async::context ctx;

    auto* displayGroup = app.add_option_group("Display RBMC information");
    auto* flag =
        displayGroup->add_flag("-d", info, "Display basic RBMC information");
    displayGroup->add_flag("-e", extended, "Add in extended details")
        ->needs(flag);
    displayGroup->add_flag("-j, --json", jsonOutput, "Display in JSON")
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
        ctx.spawn(displayInfo(ctx, extended, jsonOutput));
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
