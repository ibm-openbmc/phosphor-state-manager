// SPDX-License-Identifier: Apache-2.0

#include "util.hpp"

#include "persistent_data.hpp"
#include "phosphor-logging/lg2.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <xyz/openbmc_project/Inventory/Item/System/common.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

#include <cstdint>
#include <format>
#include <fstream>
#include <ranges>
#include <string>
#include <vector>

namespace rbmc::util
{

using ObjectMapper = sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

RedundancyInputSet readExternalRedundancyInputs()
{
    try
    {
        // data::read uses the underlying type
        using UnderlyingSet = std::set<std::underlying_type_t<RedundancyInput>>;
        auto underlyingInputs =
            data::read<UnderlyingSet>(data::key::externalRedundancyInputs);
        if (underlyingInputs.has_value())
        {
            RedundancyInputSet enumInputs;
            for (const auto& val : underlyingInputs.value())
            {
                enumInputs.insert(static_cast<RedundancyInput>(val));
            }
            return enumInputs;
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Could not read external redundancy inputs: {ERROR}",
                   "ERROR", e);
    }
    return RedundancyInputSet{};
}

bool hasExternalRedundancyInput(RedundancyInput input)
{
    try
    {
        auto inputs = readExternalRedundancyInputs();
        return inputs.contains(input);
    }
    catch (const std::exception& e)
    {
        lg2::error("Could not read external redundancy input: {ERROR}", "ERROR",
                   e);
    }
    return false;
}

void writeExternalRedundancyInput(RedundancyInput input, bool set)
{
    RedundancyInputSet inputs;
    try
    {
        inputs = readExternalRedundancyInputs();
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed trying to obtain saved RedundancyInput value: {ERROR}",
            "ERROR", e);
    }

    if (set)
    {
        inputs.insert(input);
    }
    else
    {
        inputs.erase(input);
    }

    // data::write uses the underlying type
    using UnderlyingSet = std::set<std::underlying_type_t<RedundancyInput>>;
    UnderlyingSet underlyingInputs;
    for (const auto& enumVal : inputs)
    {
        underlyingInputs.insert(std::to_underlying(enumVal));
    }

    try
    {
        data::write(data::key::externalRedundancyInputs, underlyingInputs);
    }
    catch (const std::exception& e)
    {
        lg2::error("Could not serialize RedundancyInput value: {ERROR}",
                   "ERROR", e);
        throw;
    }
}

std::string uptimeToString(uint64_t total)
{
    constexpr uint64_t secondsPerMinute = 60;
    constexpr uint64_t secondsPerHour = 60 * secondsPerMinute;
    constexpr uint64_t secondsPerDay = 24 * secondsPerHour;

    // Full days elapsed
    auto days = total / secondsPerDay;
    // Hours portion of the remainder within a day
    auto hours = (total % secondsPerDay) / secondsPerHour;
    // Minutes portion of the remainder within an hour
    auto minutes = (total % secondsPerHour) / secondsPerMinute;

    std::vector<std::string> parts;
    if (days > 0)
    {
        parts.emplace_back(std::format("{}d", days));
    }
    if (hours > 0)
    {
        parts.emplace_back(std::format("{}h", hours));
    }
    if (minutes > 0)
    {
        parts.emplace_back(std::format("{}m", minutes));
    }
    if (parts.empty())
    {
        parts.emplace_back("0m");
    }

    auto joined = parts | std::views::join_with(' ');
    return {joined.begin(), joined.end()};
}

bool clearExternalRedundancyInputs()
{
    try
    {
        auto inputs = readExternalRedundancyInputs();

        if (!inputs.empty())
        {
            data::remove(data::key::externalRedundancyInputs);
            return true;
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Could not clear external redundancy inputs: {ERROR}",
                   "ERROR", e);
    }
    return false;
}

bool validateFailoverRedundancyInput(const FailoverOptions& options)
{
    using Failover = sdbusplus::common::xyz::openbmc_project::control::Failover;
    using RedundancyInterface =
        sdbusplus::common::xyz::openbmc_project::state::bmc::Redundancy;

    auto redInputString = getFailoverOption<std::string>(
        Failover::Options::UseRedundancyInput, options);

    if (!redInputString.has_value())
    {
        return true;
    }

    auto redInput = RedundancyInterface::convertStringToRedundancyInput(
        redInputString.value());

    if (!redInput.has_value())
    {
        lg2::error(
            "Invalid redundancy input {INPUT} passed in as failover option",
            "INPUT", redInputString.value());
        return false;
    }

    return true;
}

std::optional<std::string> getOSReleaseValue(const std::string& filePath,
                                             const std::string& key)
{
    std::ifstream file{filePath};
    if (!file.is_open())
    {
        lg2::error("Failed to open file: {FILE}", "FILE", filePath);
        return std::nullopt;
    }

    // Append '=' to the key for matching
    std::string keyPattern = key + "=";

    std::string line;
    while (std::getline(file, line))
    {
        // Check if line starts with the key pattern
        if (line.substr(0, keyPattern.size()).find(keyPattern) !=
            std::string::npos)
        {
            // Extract the value after the key pattern
            auto value = line.substr(keyPattern.size());

            // Handle quotes around the value
            // If the value isn't surrounded by quotes, then pos will be
            // npos + 1 = 0, and the 2nd arg to substr() will be npos
            // which means get the rest of the string.
            std::size_t pos = value.find_first_of('"') + 1;
            return value.substr(pos, value.find_last_of('"') - pos);
        }
    }

    return std::nullopt;
}

// NOLINTBEGIN(clang-analyzer-core.uninitialized.Branch)
sdbusplus::async::task<int> runAsyncCmd(sdbusplus::async::context& ctx,
                                        const std::string& cmd)
{
    int pipeFDs[2];

    // Open the read and write pipes
    if (pipe(pipeFDs) == -1)
    {
        auto e = errno;
        lg2::error("runAsyncCmd: pipe() failed with errno: {ERRNO}", "ERRNO",
                   e);
        co_return -1;
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        auto e = errno;
        close(pipeFDs[0]);
        close(pipeFDs[1]);
        lg2::error("runAsyncCmd: fork failed with errno {ERRNO}", "ERRNO", e);
        co_return -1;
    }
    else if (pid == 0)
    {
        // Child

        // Close the read pipe
        close(pipeFDs[0]);

        // NOLINTNEXTLINE(cert-env33-c)
        int rc = std::system(cmd.c_str());

        int exitCode = (rc == -1) ? -1 : (WIFEXITED(rc) ? WEXITSTATUS(rc) : -1);

        // Write the exit code to the write pipe
        ssize_t s = write(pipeFDs[1], &exitCode, sizeof(exitCode));

        _exit((s == sizeof(rc)) ? 0 : 1);
    }

    // In the parent here.

    // close the write pipe
    close(pipeFDs[1]);

    // Async wait for the child to write the command's rc to the read pipe
    sdbusplus::async::fdio fdio(ctx, pipeFDs[0]);
    co_await fdio.next();

    int cmdRC = -1;
    ssize_t bytesRead = read(pipeFDs[0], &cmdRC, sizeof(cmdRC));
    close(pipeFDs[0]);

    if (bytesRead != sizeof(cmdRC))
    {
        lg2::error("runAsyncCmd: Failed to read return code from command {CMD}",
                   "CMD", cmd);
        co_return -1;
    }

    // Wait for child to exit
    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
        lg2::error("runAsyncCmd: waitpid failed for command {CMD}", "CMD", cmd);
        co_return -1;
    }

    co_return cmdRC;
}
// NOLINTEND(clang-analyzer-core.uninitialized.Branch)

sdbusplus::async::task<SubTreeMap> getSubTree(
    sdbusplus::async::context& ctx, const std::string& path, int depth,
    const std::string& interface)
{
    auto mapper = ObjectMapper(ctx)
                      .service(ObjectMapper::default_service)
                      .path(ObjectMapper::instance_path);

    co_return co_await mapper.get_sub_tree(path, depth, {interface});
}

sdbusplus::async::task<std::string> getService(sdbusplus::async::context& ctx,
                                               const std::string& path,
                                               const std::string& interface)
{
    auto mapper = ObjectMapper(ctx)
                      .service(ObjectMapper::default_service)
                      .path(ObjectMapper::instance_path);

    auto object = co_await mapper.get_object(path, {interface});
    co_return object.begin()->first;
}

sdbusplus::async::task<std::string> findSystemInventoryPath(
    sdbusplus::async::context& ctx)
{
    using SystemInv =
        sdbusplus::common::xyz::openbmc_project::inventory::item::System;

    auto objects = co_await getSubTree(ctx, "/xyz/openbmc_project/inventory", 0,
                                       SystemInv::interface);

    if (objects.empty())
    {
        throw std::runtime_error("No system inventory object found");
    }

    // Until there is a reason to expect more, check
    // that there is just one System interface.
    if (objects.size() != 1)
    {
        throw std::invalid_argument(std::format(
            "Wrong number of system inventory objects: {}", objects.size()));
    }

    co_return objects.begin()->first;
}

} // namespace rbmc::util
