// SPDX-License-Identifier: Apache-2.0
#include "pcie_storage.hpp"

#include "phosphor-logging/lg2.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace pcie_data
{
PCIeStorageImpl::PCIeStorageImpl(const std::string& devPath, size_t offset) :
    devicePath(devPath), redundancyOffset(offset)
{
    try
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        fd = open(devicePath.c_str(), O_RDWR | O_CLOEXEC);
        if (fd == -1)
        {
            lg2::error("PCIe device not available at {PATH}", "PATH",
                       devicePath);
            return;
        }

        mmioBase = mmap(nullptr, redundancySize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, static_cast<off_t>(redundancyOffset));
        if (mmioBase == MAP_FAILED)
        {
            close(fd);
            fd = -1;
            lg2::error("PCIe mmap failed at offset {OFFSET}", "OFFSET",
                       redundancyOffset);
            return;
        }

        lg2::debug(
            "PCIe storage initialized successfully at {PATH} offset {OFFSET}",
            "PATH", devicePath, "OFFSET", redundancyOffset);
    }
    catch (const std::exception& e)
    {
        lg2::error("PCIe storage initialization failed: {ERROR}", "ERROR", e);
    }
}

PCIeStorageImpl::~PCIeStorageImpl()
{
    if (mmioBase != nullptr && mmioBase != MAP_FAILED)
    {
        munmap(mmioBase, redundancySize);
    }
    if (fd != -1)
    {
        close(fd);
    }
}

void PCIeStorageImpl::validateMMIO() const
{
    if (mmioBase == nullptr || mmioBase == MAP_FAILED)
    {
        throw std::runtime_error("PCIe storage not initialized");
    }
}

void PCIeStorageImpl::writeState(const RedundancyState& state)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    validateMMIO();

    RedundancyState modifiedState = state;
    modifiedState.version = redundancyDataVersion;

    std::memcpy(mmioBase, &modifiedState, sizeof(RedundancyState));
    __sync_synchronize();
}

RedundancyState PCIeStorageImpl::readState()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    validateMMIO();

    RedundancyState state{};
    std::memcpy(&state, mmioBase, sizeof(RedundancyState));

    return state;
}

void PCIeStorageImpl::updateRole(uint8_t role)
{
    auto state = readState();
    state.role = role;
    writeState(state);
    lg2::debug("Role updated in PCIe memory to {VALUE}", "VALUE", role);
}

void PCIeStorageImpl::updateRedundancyEnabled(bool enabled)
{
    auto state = readState();
    state.redundancyEnabled = enabled ? 1 : 0;
    writeState(state);
    lg2::debug("RedundancyEnabled updated in PCIe memory to {VALUE}", "VALUE",
               enabled);
}

void PCIeStorageImpl::updateFailoverInProgress(bool inProgress)
{
    auto state = readState();
    state.failoverInProgress = inProgress ? 1 : 0;
    writeState(state);
    lg2::debug("FailoverInProgress updated in PCIe memory to {VALUE}", "VALUE",
               inProgress);
}

void PCIeStorageImpl::updateFailoversAllowed(bool allowed)
{
    auto state = readState();
    state.failoversAllowed = allowed ? 1 : 0;
    writeState(state);
    lg2::debug("FailoversAllowed updated in PCIe memory to {VALUE}", "VALUE",
               allowed);
}

} // namespace pcie_data
