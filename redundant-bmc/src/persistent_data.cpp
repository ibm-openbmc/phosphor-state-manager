// SPDX-License-Identifier: Apache-2.0

#include "persistent_data.hpp"

#include "phosphor-logging/lg2.hpp"

#include <format>
#include <fstream>

namespace data
{

constexpr size_t maxFailoverLogEntries = 10;

namespace util
{

std::optional<nlohmann::json> readFile(const std::filesystem::path& path)
{
    if (std::filesystem::exists(path))
    {
        std::ifstream stream{path};
        try
        {
            return nlohmann::json::parse(stream, nullptr, true);
        }
        catch (const std::exception& e)
        {
            lg2::error("Error parsing JSON in {FILE}: {ERROR}", "FILE", path,
                       "ERROR", e);
        }
    }

    return std::nullopt;
}

void writeFile(const nlohmann::json& json, const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream stream{path};
    stream << std::setw(4) << json;
    if (stream.fail())
    {
        throw std::runtime_error{
            std::format("Failed writing {}", path.string())};
    }
}

static std::filesystem::path makeFailoverLogFilePath(
    const std::filesystem::path& dirPath, size_t bmcPos)
{
    std::string filename = "bmc" + std::to_string(bmcPos) + "_failovers";
    return dirPath / filename;
}

static std::string makeTimestampString(const time_t& timestamp)
{
    tm utcTime{};
    gmtime_r(&timestamp, &utcTime);

    std::string timeBuf(50, '\0');
    auto size = strftime(timeBuf.data(), timeBuf.size(),
                         "%Y-%m-%d %H:%M:%S UTC", &utcTime);
    timeBuf.resize(size);
    return timeBuf;
}

} // namespace util

void remove(std::string_view name, const std::filesystem::path& path)
{
    auto json = util::readFile(path);
    if (!json)
    {
        return;
    }

    if (json->erase(name) != 0)
    {
        util::writeFile(json.value(), path);
    }
}

void logFailover(const std::filesystem::path& dirPath, size_t bmcPos,
                 Requester requester, const time_t& timestamp)
{
    try
    {
        auto file = util::makeFailoverLogFilePath(dirPath, bmcPos);
        auto logs = util::readFile(file);

        if (!logs.has_value())
        {
            logs = nlohmann::json::array();
        }

        if (!logs->is_array())
        {
            lg2::error("failover log {FILE} isn't a JSON array", "FILE", file);
            return;
        }

        // If at the max number of entries, drop the oldest.
        if (logs->size() == maxFailoverLogEntries)
        {
            logs->erase(logs->begin());
        }

        // Just save the last segment of the enum string, e.g. 'Host'.
        auto r = sdbusplus::common::xyz::openbmc_project::control::Failover::
            convertRequesterToString(requester);
        r = r.substr(r.find_last_of('.') + 1);

        nlohmann::json log = {
            {"requester", r},
            {"timestamp", util::makeTimestampString(timestamp)}};
        logs->push_back(std::move(log));
        util::writeFile(logs.value(), file);
    }
    catch (const std::exception& e)
    {
        lg2::error("Error while logging a failover: {ERROR}", "ERROR", e);
    }
}

FailoverLogs getFailoverLogs(const std::filesystem::path& dirPath,
                             size_t bmcPos)
{
    FailoverLogs logs;
    auto jsonLogs =
        util::readFile(util::makeFailoverLogFilePath(dirPath, bmcPos));

    if (!jsonLogs.has_value())
    {
        return logs;
    }

    std::ranges::transform(
        jsonLogs.value(), std::back_inserter(logs), [](const auto& log) {
            std::string requester = log.value("requester", "bad data");
            std::string timestamp = log.value("timestamp", "bad data");
            return std::pair{requester, timestamp};
        });

    return logs;
}

} // namespace data
