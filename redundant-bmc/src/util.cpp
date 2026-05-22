// SPDX-License-Identifier: Apache-2.0

#include "util.hpp"

#include "persistent_data.hpp"
#include "phosphor-logging/lg2.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <fstream>

namespace rbmc::util
{

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

} // namespace rbmc::util
