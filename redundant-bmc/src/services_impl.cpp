/* SPDX-License-Identifier: Apache-2.0 */
#include "services_impl.hpp"

#include <phosphor-logging/lg2.hpp>

namespace rbmc
{

size_t ServicesImpl::getBMCPosition() const
{
    size_t bmcPosition = 0;

    // NOTE:  This a temporary solution for simulation until the
    // daemon that should be providing this information is in place.
    // Most likely, this will then have to return a task<uint32_t>.

    // Read it out of the bmc_position uboot environment variable
    // This was written by a simulation script.
    std::string cmd{"/sbin/fw_printenv -n bmc_position"};

    // NOLINTBEGIN(cert-env33-c)
    FILE* pipe = popen(cmd.c_str(), "r");
    // NOLINTEND(cert-env33-c)
    if (pipe == nullptr)
    {
        throw std::runtime_error("Error calling popen to get bmc_position");
    }

    std::string output;
    std::array<char, 128> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        output.append(buffer.data());
    }

    int rc = pclose(pipe);
    if (WEXITSTATUS(rc) != 0)
    {
        throw std::runtime_error{std::format(
            "Error running cmd: {}, output = {}, rc = {}", cmd, output, rc)};
    }

    auto [_,
          ec] = std::from_chars(&*output.begin(), &*output.end(), bmcPosition);
    if (ec != std::errc())
    {
        throw std::runtime_error{
            std::format("Could not extract position from {}: rc {}", output,
                        std::to_underlying(ec))};
    }

    return bmcPosition;
}

} // namespace rbmc
