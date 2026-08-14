// SPDX-License-Identifier: Apache-2.0

#include "config_parser.hpp"
#include "pcie_storage.hpp"
#include "persistent_data.hpp"
#include "redundancy.hpp"
#include "services_impl.hpp"
#include "sibling_reset_impl.hpp"
#include "types.hpp"
#include "util.hpp"
#include "wait_tracker.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Control/Failover/client.hpp>
#include <xyz/openbmc_project/Provisioning/Provisioning/client.hpp>
#include <xyz/openbmc_project/Software/Version/client.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>
#include <xyz/openbmc_project/State/BMC/client.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <print>

using Redundancy =
    sdbusplus::client::xyz::openbmc_project::state::bmc::Redundancy<>;
using BMCState = sdbusplus::client::xyz::openbmc_project::state::BMC<>;
using Role = Redundancy::Role;
using Failover = sdbusplus::client::xyz::openbmc_project::control::Failover<>;
using Version = sdbusplus::client::xyz::openbmc_project::software::Version<>;
using Pairing =
    sdbusplus::client::xyz::openbmc_project::provisioning::Provisioning<>;
using PeerConnectionStatus = sdbusplus::common::xyz::openbmc_project::
    provisioning::Provisioning::PeerConnectionStatus;

constexpr auto siblingService =
    "xyz.openbmc_project.State.BMC.Redundancy.Sibling";
constexpr auto pairingService = "xyz.openbmc_project.Provisioning";

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

template <typename T>
std::string getPDIEnumString(T value)
    requires(std::is_enum_v<T>)
{
    try
    {
        auto v = sdbusplus::message::convert_to_string(value);
        return v.substr(v.find_last_of('.') + 1);
    }
    catch (const std::exception& e)
    {
        return "BadEnum:" + std::to_string(std::to_underlying(value));
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<std::string> getBMCState(const rbmc::Services& services)
{
    try
    {
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        auto bmcState = co_await services.getBMCState();

        co_return getPDIEnumString(bmcState);
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
            reasonList.push_back(getPDIEnumString(r));
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

void addFONotAllowedReason(rbmc::FailoversNotAllowedReason reason,
                           nlohmann::ordered_json& output)
{
    // The output looks better as an array even though there
    // is only one value.
    nlohmann::json::array_t reasonList;
    reasonList.emplace_back(getPDIEnumString(reason));

    output["Reason failovers are not allowed"] = std::move(reasonList);
}

void addExternalRedundancyInputs(nlohmann::ordered_json& output)
{
    auto inputs = rbmc::util::readExternalRedundancyInputs();

    if (!inputs.empty())
    {
        nlohmann::json::array_t inputList;
        for (const auto& input : inputs)
        {
            inputList.emplace_back(getPDIEnumString(input));
        }
        output["External Redundancy Inputs"] = std::move(inputList);
    }
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

void addActiveWaits(nlohmann::ordered_json& output)
{
    auto waits = rbmc::WaitTracker::readWaits(data::dataDirPath);

    if (!waits.empty())
    {
        nlohmann::json::array_t waitList;
        for (const auto& wait : waits)
        {
            waitList.emplace_back(rbmc::waitOperationToString(wait.operation));
        }
        output["Active Waits"] = std::move(waitList);
    }
}

void addBMCUptime(nlohmann::ordered_json& output)
{
    std::ifstream f("/proc/uptime");
    if (!f)
    {
        return;
    }

    // Read only the integer seconds; kernel writes e.g. "5416.64 0.22"
    uint64_t total{};
    if (!(f >> total))
    {
        return;
    }

    output["BMC Uptime"] = rbmc::util::uptimeToString(total);
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

        auto role = getPDIEnumString(props.role);
        output["Role"] = role;

        rbmc::WaitTracker waitTracker;
        rbmc::ServicesImpl services{ctx, waitTracker};
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

        try
        {
            auto pairingProps = co_await Pairing(ctx)
                                    .service(pairingService)
                                    .path(Pairing::instance_path)
                                    .properties();
            if (!pairingProps.provisioned)
            {
                output["Paired"] = pairingProps.provisioned;
            }

            if (pairingProps.peer_connected != PeerConnectionStatus::Connected)
            {
                output["Peer Connected"] =
                    getPDIEnumString(pairingProps.peer_connected);
            }
        }
        catch (const std::exception& e)
        {
            output["Paired"] = e.what();
            output["Peer Connected"] = e.what();
        }

        if (data::read<bool>(data::key::codeUpdateInProgress).value_or(false))
        {
            output["In Code Update"] = true;
        }

        if (role != "Unknown")
        {
            output["Role Reason"] =
                data::read<std::string>(data::key::roleReason)
                    .value_or("No reason found");
        }

        addBMCUptime(output);

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
            addFONotAllowedReason(props.failovers_not_allowed_reason, output);
        }

        if (role == "Active")
        {
            addExternalRedundancyInputs(output);
        }

        addActiveWaits(output);

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

        output["Role"] = getPDIEnumString(rProps.role);

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

        auto pairingProps = co_await Pairing(ctx)
                                .service(siblingService)
                                .path(path.str)
                                .properties();

        output["Redundancy Enabled"] = rProps.redundancy_enabled;
        output["Failovers Allowed"] = rProps.failovers_allowed;
        output["BMC State"] = getPDIEnumString(state);
        output["FW Version Hash"] = fwVersion;
        if (!pairingProps.provisioned)
        {
            output["Paired"] = pairingProps.provisioned;
        }
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

void displayWaitStatusDetailed()
{
    auto waits = rbmc::WaitTracker::readWaits(data::dataDirPath);

    std::println();

    if (waits.empty())
    {
        std::println("No active wait operations");
    }
    else
    {
        std::println("Active Wait Operations");
        std::println("-----------------------------");

        for (const auto& wait : waits)
        {
            std::println();
            printParam("Operation",
                       rbmc::waitOperationToString(wait.operation));

            // Calculate elapsed time
            auto now = std::chrono::steady_clock::now();
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch())
                             .count();
            auto elapsedMs = nowMs - wait.startTimeMs;
            auto elapsedSec = elapsedMs / 1000;

            printParam("Elapsed (s)", elapsedSec);
            printParam("Timeout (s)", wait.timeoutSeconds);
        }
    }

    std::println();
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
    auto config = rbmc::config_parser::readConfig();
    rbmc::SiblingResetImpl reset{ctx, config};

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

void displayPCIeState()
{
    try
    {
        auto config = rbmc::config_parser::readConfig();

        if (!config.pcieConfig.has_value())
        {
            std::println("Error: PCIe storage not configured in config file");
            exit(EXIT_FAILURE);
        }

        const auto& pcieConf = config.pcieConfig.value();

        // Parse offset string (supports decimal and hex with 0x prefix)
        size_t offset = std::stoull(pcieConf.redundancyOffset, nullptr, 0);

        pcie_data::PCIeStorageImpl pcieStorage(pcieConf.devicePath, offset);
        auto state = pcieStorage.readState();

        std::println();
        std::println("PCIe MMIO Redundancy State");
        std::println("-----------------------------");

        printParam("Version", static_cast<int>(state.version));
        printParam("Role", getPDIEnumString(static_cast<Role>(state.role)));
        printParam("Redundancy Enabled",
                   static_cast<bool>(state.redundancyEnabled));
        printParam("Failover In Progress",
                   static_cast<bool>(state.failoverInProgress));
        printParam("Failovers Allowed",
                   static_cast<bool>(state.failoversAllowed));

        // Display raw byte value for debugging
        uint8_t rawByte;
        std::memcpy(&rawByte, &state, sizeof(state));
        std::println("{:22}0b{:08b} (0x{:02X})", "Raw Byte Value:", rawByte,
                     rawByte);

        std::println();
    }
    catch (const std::exception& e)
    {
        std::println("Error reading PCIe storage: {}", e.what());
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv)
{
    CLI::App app{"RBMC Tool"};
    bool info{};
    bool extended{};
    bool waitStatus{};
    bool resetSibling{};
    bool disableRedundancy{};
    bool enableRedundancy{};
    bool failover{};
    bool forceFailover{};
    bool jsonOutput{};
    bool readPCIe{};
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

    auto* pcieGroup = app.add_option_group("PCIe MMIO operations");
    pcieGroup->add_flag("-p, --read-pcie", readPCIe,
                        "Read redundancy state from PCIe MMIO");

    auto* waitGroup = app.add_option_group("Display wait status");
    waitGroup->add_flag("-w, --wait-status", waitStatus,
                        "Display detailed active wait operations");

    app.require_option(1);

    CLI11_PARSE(app, argc, argv);

    if (info)
    {
        ctx.spawn(displayInfo(ctx, extended, jsonOutput));
    }
    else if (readPCIe)
    {
        displayPCIeState();
        return 0;
    }
    else if (waitStatus)
    {
        displayWaitStatusDetailed();
        return 0;
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
