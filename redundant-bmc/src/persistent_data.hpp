// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/Control/Failover/common.hpp>

#include <filesystem>
#include <optional>

namespace data
{

// Default path for persistent data directory
inline std::filesystem::path dataDirPath{
    "/var/lib/phosphor-state-manager/redundant-bmc"};

constexpr auto dataFileName = "data.json";

/**
 * @brief Get the full path to the data file
 *
 * @return The full path combining dataDirPath and dataFileName
 */
inline std::filesystem::path dataFile()
{
    return dataDirPath / dataFileName;
}

/**
 * @brief Set the persistent data directory path
 *
 * This should be called early in initialization to set the directory
 * where persistent data files will be stored.
 *
 * @param[in] dirPath - The directory path for persistent data
 */
inline void setDataDirectory(const std::filesystem::path& dirPath)
{
    dataDirPath = dirPath;
}

using FailoverLogs = std::vector<std::pair<std::string, std::string>>;

using Requester =
    sdbusplus::common::xyz::openbmc_project::control::Failover::Requester;

namespace key
{
constexpr auto role = "Role";
constexpr auto passiveError = "PassiveDueToError";
constexpr auto roleReason = "RoleReason";
constexpr auto noRedReasons = "NoRedundancyReasons";
constexpr auto disableRed = "DisableRedundancy";
constexpr auto redundancyOffAtRuntime = "RedundancyOffAtRuntime";
constexpr auto failoverInProgress = "FailoverInProgress";
} // namespace key

namespace util
{

/**
 * @brief Helper function to read a JSON file
 *
 * @param[in] path - The path to the file
 *
 * @return optional<json> - The JSON data, or std::nullopt if it
 *                          didn't exist or was corrupt.
 */
std::optional<nlohmann::json> readFile(const std::filesystem::path& path);

/**
 * @brief Helper function to write a JSON file
 *
 * @param[in] json - The JSON to write
 * @param[in] path - The path to the file
 */
void writeFile(const nlohmann::json& json, const std::filesystem::path& path);

} // namespace util

/**
 * @brief Writes "name": <value>  JSON to the file specified
 *
 * Throws an exception on error.
 *
 * @tparam - The data type
 * @param[in] name - The key to save the value under
 * @param[in] value - The value to save
 */
template <typename T>
void write(std::string_view name, const T& value)
{
    auto path = dataFile();
    auto json = util::readFile(path).value_or(nlohmann::json::object());
    if constexpr (std::is_enum_v<T>)
    {
        json[name] = std::to_underlying(value);
    }
    else
    {
        json[name] = value;
    }

    util::writeFile(json, path);
}

/**
 * @brief Reads the value of the key specified in the file specified
 *
 * Throws an exception on error.
 *
 * @tparam T - The data type
 * @param[in] name - The key the value is saved under
 *
 * @return optional<T> - The value, or std::nullopt if the file or
 *                       key isn't present.
 */
template <typename T>
std::optional<T> read(std::string_view name)
{
    auto json = util::readFile(dataFile());
    if (!json)
    {
        return std::nullopt;
    }

    auto it = json->find(name);
    if (it != json->end())
    {
        if constexpr (std::is_enum_v<T>)
        {
            auto value = it->get<std::underlying_type_t<T>>();
            return static_cast<T>(value);
        }
        else
        {
            return it->get<T>();
        }
    }

    return std::nullopt;
}

/**
 * @brief Remove an entry from the file
 *
 * @param[in] name - The key for the entry to remove
 */
void remove(std::string_view name);

/**
 * @brief Log the requester and timestamp of a failover.
 *
 * The BMC driving the failover uses this.  It will keep the 10 most
 * recent entries in a file called bmc<position>_failovers.
 *
 * @param[in] dirPath - The directory to save the log file in.
 * @param[in] bmcPos - The BMC position driving the failover.
 * @param[in] requester - The failover requester, e.g. 'host'.
 * @param[in] timestamp - The time the failover was initiated.
 */
void logFailover(const std::filesystem::path& dirPath, size_t bmcPos,
                 Requester requester, const time_t& timestamp);

/**
 * @brief Returns the failover logs as a vector of pairs.
 *
 * pair.first is the requester, and pair.second is the UTC timestamp
 * that looks like: "YYYY-MM-DD HH:MM:SS UTC"
 *
 * @param[in] dirPath - The directory to save the log file in.
 * @param[in] bmcPos - The BMC position driving the failover.
 */
FailoverLogs getFailoverLogs(const std::filesystem::path& dirPath,
                             size_t bmcPos);

} // namespace data
