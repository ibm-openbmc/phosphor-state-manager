// SPDX-License-Identifier: Apache-2.0
#include "wait_tracker.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <fstream>

namespace rbmc
{

constexpr auto waitStatusFileName = "wait_status.json";

std::string waitOperationToString(WaitOperation op)
{
    std::string str{"Unknown"};
    switch (op)
    {
        case WaitOperation::systemInventoryPath:
            str = "SystemInventoryPath";
            break;
        case WaitOperation::systemInventoryStatus:
            str = "SystemInventoryStatus";
            break;
        case WaitOperation::peerConnection:
            str = "PeerConnection";
            break;
        case WaitOperation::selfPairing:
            str = "SelfPairing";
            break;
        case WaitOperation::startUnit:
            str = "StartUnit";
            break;
        case WaitOperation::siblingAlive:
            str = "SiblingAlive";
            break;
        case WaitOperation::siblingBMCSteadyState:
            str = "SiblingBMCSteadyState";
            break;
        case WaitOperation::siblingHealthTimer:
            str = "SiblingHealthTimer";
            break;
        case WaitOperation::peerConnectionTimer:
            str = "PeerConnectionTimer";
            break;
        case WaitOperation::bmcResetTimer:
            str = "BMCResetTimer";
            break;
        case WaitOperation::fullSync:
            str = "FullSync";
            break;
    }

    return str;
}

WaitTracker::WaitTracker(const std::filesystem::path& dirPath) :
    filePath(dirPath.empty() ? std::filesystem::path{}
                             : dirPath / waitStatusFileName),
    enabled(!filePath.empty())
{
    // Clear any stale wait data
    if (enabled)
    {
        updateFile();
    }
}

void WaitTracker::enableTracking(const std::filesystem::path& dirPath)
{
    if (enabled)
    {
        return;
    }

    if (dirPath.empty())
    {
        lg2::warning("Cannot enable WaitTracker with empty directory path");
        return;
    }

    filePath = dirPath / waitStatusFileName;
    enabled = true;

    // Clear any stale wait data
    updateFile();
}

uint64_t WaitTracker::registerWait(WaitOperation operation,
                                   std::chrono::seconds timeout)
{
    if (!enabled)
    {
        return 0;
    }

    uint64_t id = counter++;

    // Handle a rollover
    if (counter == 0)
    {
        counter = 1;
    }

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    activeWaits[id] = WaitInfo{operation, static_cast<uint64_t>(nowMs),
                               static_cast<uint32_t>(timeout.count())};
    updateFile();

    return id;
}

void WaitTracker::unregisterWait(uint64_t id)
{
    if (!enabled || id == 0)
    {
        return;
    }

    activeWaits.erase(id);
    updateFile();
}

void WaitTracker::updateFile()
{
    try
    {
        // If no active waits, just remove the file
        if (activeWaits.empty())
        {
            if (std::filesystem::exists(filePath))
            {
                std::filesystem::remove(filePath);
            }
            return;
        }

        nlohmann::json j;
        nlohmann::json::array_t waitsArray;

        for (const auto& [id, info] : activeWaits)
        {
            nlohmann::json waitObj;
            waitObj["id"] = id;
            waitObj["operationEnum"] = static_cast<int>(info.operation);
            waitObj["startTimeMs"] = info.startTimeMs;
            waitObj["timeoutSeconds"] = info.timeoutSeconds;
            waitsArray.push_back(waitObj);
        }

        j["waits"] = waitsArray;

        // Create parent directory if it doesn't exist
        std::filesystem::create_directories(filePath.parent_path());

        std::ofstream ofs(filePath);
        if (!ofs)
        {
            lg2::warning("Failed to open wait tracker file {PATH}", "PATH",
                         filePath);
            return;
        }

        ofs << j.dump(4);
    }
    catch (const std::exception& e)
    {
        lg2::warning("Failed to update wait tracker file: {ERROR}", "ERROR", e);
    }
}

std::vector<WaitTracker::WaitInfo> WaitTracker::readWaits(
    const std::filesystem::path& dirPath)
{
    std::vector<WaitInfo> result;

    auto filePath = dirPath / waitStatusFileName;

    try
    {
        if (!std::filesystem::exists(filePath))
        {
            return result;
        }

        std::ifstream ifs(filePath);
        if (!ifs)
        {
            lg2::error("Could not open wait tracking file {FILE}", "FILE",
                       filePath);
            return result;
        }

        nlohmann::json j;
        ifs >> j;

        if (!j.contains("waits") || !j["waits"].is_array())
        {
            return result;
        }

        for (const auto& waitObj : j["waits"])
        {
            WaitInfo info;
            info.operation =
                static_cast<WaitOperation>(waitObj["operationEnum"].get<int>());
            info.startTimeMs = waitObj["startTimeMs"].get<uint64_t>();
            info.timeoutSeconds = waitObj["timeoutSeconds"].get<uint32_t>();
            result.push_back(info);
        }
    }
    catch (const std::exception& e)
    {
        lg2::warning("Failed to read wait tracker file: {ERROR}", "ERROR", e);
    }

    return result;
}

} // namespace rbmc
