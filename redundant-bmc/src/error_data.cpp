// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "error_data.hpp"

#include "persistent_data.hpp"

namespace rbmc::errors
{

namespace
{

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

std::string boolToYesOrNo(bool value)
{
    return value ? "Yes" : "No";
}

void addRedundancyData(const RedundancyInterface& iface, AdditionalData& data)
{
    data["Role"] = getPDIEnumString(iface.role());
    data["RedEnabled"] = boolToYesOrNo(iface.redundancy_enabled());
    data["FOAllowed"] = boolToYesOrNo(iface.failovers_allowed());

    if (!iface.reasons_for_no_redundancy().empty())
    {
        data["RedDisabledReasons"] = std::ranges::fold_left(
            iface.reasons_for_no_redundancy(), std::string{},
            [](const auto& front, auto r) {
                auto enumString = getPDIEnumString(r);
                return front.empty() ? enumString : front + ' ' + enumString;
            });
    }

    if (iface.failovers_not_allowed_reason() !=
        RedundancyInterface::FailoversNotAllowedReason::None)
    {
        data["FONotAllowedReason"] =
            getPDIEnumString(iface.failovers_not_allowed_reason());
    }

    // Only save these when they're interesting.
    if (iface.failover_in_progress())
    {
        data["FOInProgress"] = boolToYesOrNo(true);
    }
    if (iface.failover_imminent())
    {
        data["FOImminent"] = boolToYesOrNo(true);
    }
}

void addSiblingData(const Sibling& sibling, AdditionalData& data)
{
    if (!sibling.alive())
    {
        data["SibStatus"] = "NotResponding";
        return;
    }

    auto roleOpt = sibling.getRole();
    if (roleOpt.has_value())
    {
        data["SibRole"] = getPDIEnumString(roleOpt.value());
    }

    auto reOpt = sibling.getRedundancyEnabled();
    if (reOpt.has_value())
    {
        data["SibRedEnabled"] = boolToYesOrNo(reOpt.value());
    }

    auto foaOpt = sibling.getFailoversAllowed();
    if (foaOpt.has_value())
    {
        data["SibFOAllowed"] = boolToYesOrNo(foaOpt.value());
    }

    if (sibling.getFailoverInProgress().value_or(false))
    {
        data["SibFOInProgress"] = boolToYesOrNo(true);
    }

    if (sibling.getFailoverImminent().value_or(false))
    {
        data["SibFOImminent"] = boolToYesOrNo(true);
    }

    if (sibling.getHasReasonForNoRedundancy().value_or(false))
    {
        data["SibHasReasonForNoRed"] = boolToYesOrNo(true);
    }

    auto stateOpt = sibling.getBMCState();
    if (stateOpt.has_value())
    {
        data["SibBMCState"] = getPDIEnumString(stateOpt.value());
    }
}

void addServicesData(const Services& services, bool isActive,
                     AdditionalData& data)
{
    data["BMCPos"] = services.getBMCPosition()
                         .transform([](auto p) { return std::to_string(p); })
                         .value_or("Unknown");
    if (isActive)
    {
        try
        {
            data["SysState"] =
                Services::getSystemStateName(services.getSystemState());
        }
        catch (const std::exception& e)
        {
            data["SysStateError"] = e.what();
        }
    }

    auto logs = data::getFailoverLogs(services.getPersistentDataPath(), 0);
    if (!logs.empty())
    {
        const auto& last = logs.back();
        data["BMC0PrevFO"] = std::format("{} {}", last.first, last.second);
    }

    logs = data::getFailoverLogs(services.getPersistentDataPath(), 1);
    if (!logs.empty())
    {
        const auto& last = logs.back();
        data["BMC1PrevFO"] = std::format("{} {}", last.first, last.second);
    }
}

void addFileData(AdditionalData& data)
{
    try
    {
        auto reason = data::read<std::string>(data::key::roleReason);
        if (reason.has_value())
        {
            data["RoleReason"] = reason.value();
        }

        auto redAtRuntime = data::read<std::tuple<bool, bool>>(
            data::key::redundancyOffAtRuntime);

        if (redAtRuntime.has_value())
        {
            const auto& [valid, off] = redAtRuntime.value();
            if (valid && off)
            {
                data["RedOffAtRuntime"] = boolToYesOrNo(true);
            }
        }
    }
    catch (const std::exception& e)
    {
        data["DataError"] = e.what();
    }
}

void addFWData(const Services& services, const Sibling& sibling,
               AdditionalData& data)
{
    if (!sibling.alive())
    {
        return;
    }

    auto localVersion = services.getFWVersion();
    auto sibVersion = sibling.getFWVersion().value_or("DEAD");
    if (localVersion != sibVersion)
    {
        data["FWVersionMismatch"] =
            std::format("Local: {} Sibling: {}", localVersion, sibVersion);
    }
}

} // namespace

void addDefaultData(const RedundancyInterface& iface, Providers& providers,
                    AdditionalData& data)
{
    data.emplace("_PID", std::to_string(getpid()));
    addRedundancyData(iface, data);
    addSiblingData(providers.getSibling(), data);
    addServicesData(providers.getServices(),
                    iface.role() == RedundancyInterface::Role::Active, data);
    addFileData(data);
    addFWData(providers.getServices(), providers.getSibling(), data);
}

void addFailoverOptsToData(const FailoverOptions& options,
                           errors::AdditionalData& data)
{
    constexpr auto failoverOptPrefix = "FOOpt:";

    for (const auto& [key, value] : options)
    {
        std::string adKey = failoverOptPrefix + key;

        data.emplace(adKey, std::visit(
                                [](auto&& val) -> std::string {
                                    using T = std::decay_t<decltype(val)>;
                                    if constexpr (std::is_same_v<T, bool>)
                                    {
                                        return val ? "true" : "false";
                                    }
                                    else
                                    {
                                        // FailoverOptions can only hold bools
                                        return "Unsupported option type";
                                    }
                                },
                                value));
    }
}

} // namespace rbmc::errors
