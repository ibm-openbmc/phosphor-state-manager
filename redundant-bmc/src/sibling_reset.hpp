// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>

namespace rbmc
{

/**
 * @class SiblingReset
 *
 * Abstract base class to handling resetting the sibling BMC.
 */
class SiblingReset
{
  public:
    SiblingReset() = default;
    virtual ~SiblingReset() = default;
    SiblingReset(const SiblingReset&) = delete;
    SiblingReset& operator=(const SiblingReset&) = delete;
    SiblingReset(SiblingReset&&) = delete;
    SiblingReset& operator=(SiblingReset&&) = delete;

    /**
     * @brief Asserts the reset.
     *
     * Releases the GPIO request on completion.
     */
    virtual void assertReset() = 0;

    /**
     * @brief Releases the reset.
     *
     * Releases the GPIO request on completion.
     */
    virtual void releaseReset() = 0;

    /**
     * @brief Toggles the GPIO to do the full reset
     */
    virtual sdbusplus::async::task<> toggleReset() = 0;
};

} // namespace rbmc
