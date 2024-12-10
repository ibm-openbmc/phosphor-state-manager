// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>

namespace data
{

const std::filesystem::path dataFile{
    "/var/lib/phosphor-state-manager/redundant-bmc/data.json"};

namespace key
{
constexpr auto role = "Role";
constexpr auto passiveError = "PassiveDueToError";
constexpr auto roleReason = "RoleReason";
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
void write(std::string_view name, T value,
           const std::filesystem::path& path = dataFile)
{
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
 * @param[in] path - The path to the file
 *
 * @return optional<T> - The value, or std::nullopt if the file or
 *                       key isn't present.
 */
template <typename T>
std::optional<T> read(std::string_view name,
                      const std::filesystem::path& path = dataFile)
{
    auto json = util::readFile(path);
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
 *
 * @param[in] path - The path to the file
 */
void remove(std::string_view name,
            const std::filesystem::path& path = dataFile);

} // namespace data
