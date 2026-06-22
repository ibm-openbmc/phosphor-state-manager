// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace rbmc
{

enum class ProgressPoint
{
    activeHandlerStartComplete,
    passiveHandlerStartComplete
};

/**
 * @class ProgressTracker
 */
class ProgressTracker
{
  public:
    ProgressTracker() = default;
    virtual ~ProgressTracker() = default;

    ProgressTracker(const ProgressTracker&) = delete;
    ProgressTracker& operator=(const ProgressTracker&) = delete;
    ProgressTracker(ProgressTracker&&) = delete;
    ProgressTracker& operator=(ProgressTracker&&) = delete;

    /**
     * @brief Track a progress point
     *
     * @param[in] event - The point to track
     */
    virtual void track(ProgressPoint /* point */) {}

    /**
     * @brief Check if the progress point has been reached
     *
     * @param[in] point - The point to check
     * @return true if it has been reached
     */
    virtual bool hasReached(ProgressPoint /* point */) const
    {
        return false;
    }
};

} // namespace rbmc
