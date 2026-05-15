// SPDX-License-Identifier: Apache-2.0

#include "util.hpp"

#include "persistent_data.hpp"
#include "phosphor-logging/lg2.hpp"

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

} // namespace rbmc::util
