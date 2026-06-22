// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "progress_tracker.hpp"

#include <set>

namespace rbmc
{

/**
 * @class TestProgressTracker
 */
class TestProgressTracker : public ProgressTracker
{
  public:
    TestProgressTracker() = default;
    ~TestProgressTracker() override = default;

    TestProgressTracker(const TestProgressTracker&) = delete;
    TestProgressTracker& operator=(const TestProgressTracker&) = delete;
    TestProgressTracker(TestProgressTracker&&) = delete;
    TestProgressTracker& operator=(TestProgressTracker&&) = delete;

    /**
     * @brief Track a progress point
     *
     * @param[in] event - The point to track
     */
    void track(ProgressPoint point) override
    {
        progress.insert(point);
    }

    /**
     * @brief Check if the progress point has been reached
     *
     * @param[in] point - The point to check
     * @return true if it has been reached
     */
    bool hasReached(ProgressPoint point) const override
    {
        return progress.contains(point);
    }

  private:
    /**
     * @brief The reached progress points
     */
    std::set<ProgressPoint> progress;
};

} // namespace rbmc
