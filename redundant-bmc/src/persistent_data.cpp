// SPDX-License-Identifier: Apache-2.0

#include "persistent_data.hpp"

#include "phosphor-logging/lg2.hpp"

#include <format>
#include <fstream>

namespace data
{
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

} // namespace data
