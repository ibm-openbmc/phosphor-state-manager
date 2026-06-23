// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace pcie_data
{
// Default values for PCIe configuration
constexpr auto defaultDevicePath = "/dev/bmc-device0";
constexpr auto defaultRedundancyOffset = 0x3EFFF80;
constexpr size_t redundancySize = 1;

constexpr uint8_t redundancyDataVersion = 1;

/**
 * @brief PCIe MMIO Redundancy State Layout
 *
 * Single-byte layout shared between BMC and host via PCIe MMIO.
 *
 * ┌─────────────────────────────────────────────────────────────┐
 * │ Bit 7   │ Bit 6      │ Bit 5    │ Bit 4-3 │ Bit 2-0 │
 * │ allowed │ inProgress │ enabled  │ role    │ version │
 * └─────────────────────────────────────────────────────────────┘
 *
 * Layout (LSB to MSB):
 *   Bits 0-2: version (0-7, current=1)
 *   Bits 3-4: role (Unknown=0, Active=1, Passive=2)
 *   Bit 5:    redundancyEnabled
 *   Bit 6:    failoverInProgress
 *   Bit 7:    failoversAllowed
 *
 * BMC Perspective:
 *   C bitfields in LSB-to-MSB order. BMC writes struct
 *   directly to PCIe MMIO with first field in lowest bits.
 *
 * Endianness Interoperability:
 *   Single byte eliminates endianness concerns. Both little-endian
 *   and big-endian hosts read identical bit pattern. No byte-order conversion
 *   needed since there's only one byte.
 *
 * Version field allows 7 future layout revisions.
 */
struct RedundancyState
{
    uint8_t version:3; // Bits 0-2: Layout version (0-7, current=1)
    uint8_t role:2;    // Bits 3-4: Role (Unknown=0, Active=1, Passive=2)
    uint8_t redundancyEnabled:1;  // Bit 5: Redundancy enabled flag
    uint8_t failoverInProgress:1; // Bit 6: Failover in progress flag
    uint8_t failoversAllowed:1;   // Bit 7: Failovers allowed flag
} __attribute__((packed));

static_assert(sizeof(RedundancyState) == redundancySize);

/**
 * @class PCIeStorage
 *
 * Abstract interface for PCIe storage operations.
 * Allows for mocking in unit tests.
 */
class PCIeStorage
{
  public:
    PCIeStorage() = default;
    virtual ~PCIeStorage() = default;
    PCIeStorage(const PCIeStorage&) = delete;
    PCIeStorage& operator=(const PCIeStorage&) = delete;
    PCIeStorage(PCIeStorage&&) = delete;
    PCIeStorage& operator=(PCIeStorage&&) = delete;

    /**
     * @brief Read the complete redundancy state from PCIe storage
     *
     * @return The redundancy state
     */
    virtual RedundancyState readState() = 0;

    /**
     * @brief Update only the role field in PCIe storage
     *
     * @param[in] role - The role value to set
     */
    virtual void updateRole(uint8_t role) = 0;

    /**
     * @brief Update only the redundancy enabled field in PCIe storage
     *
     * @param[in] enabled - The redundancy enabled value to set
     */
    virtual void updateRedundancyEnabled(bool enabled) = 0;

    /**
     * @brief Update only the failover in progress field in PCIe storage
     *
     * @param[in] inProgress - The failover in progress value to set
     */
    virtual void updateFailoverInProgress(bool inProgress) = 0;

    /**
     * @brief Update only the failovers allowed field in PCIe storage
     *
     * @param[in] allowed - The failovers allowed value to set
     */
    virtual void updateFailoversAllowed(bool allowed) = 0;
};

/**
 * @class PCIeStorageImpl
 *
 * Implementation of PCIeStorage that uses actual PCIe device.
 */
class PCIeStorageImpl : public PCIeStorage
{
  private:
    int fd{-1};
    void* mmapBase{nullptr}; // page-aligned base returned by mmap
    void* mmioBase{nullptr}; // actual byte address (mmapBase + page offset)
    size_t mmapSize{0};      // total mapped size (for munmap)
    std::mutex stateMutex;
    std::string devicePath;
    size_t redundancyOffset;

    void validateMMIO() const;

  public:
    PCIeStorageImpl(const std::string& devPath, size_t offset);
    ~PCIeStorageImpl() override;

    RedundancyState readState() override;

    void updateRole(uint8_t role) override;
    void updateRedundancyEnabled(bool enabled) override;
    void updateFailoverInProgress(bool inProgress) override;
    void updateFailoversAllowed(bool allowed) override;

  private:
    void writeState(const RedundancyState& state);
};

} // namespace pcie_data
