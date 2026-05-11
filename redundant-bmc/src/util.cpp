// SPDX-License-Identifier: Apache-2.0

#include "util.hpp"

#include "persistent_data.hpp"
#include "phosphor-logging/lg2.hpp"

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

} // namespace rbmc::util
