// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "wait_tracker.hpp"

#include <sdbusplus/async.hpp>

#include <memory>

namespace rbmc
{

/**
 * @class Timer
 *
 * A single shot timer that works with sdbusplus::async::context's event loop.
 */
class Timer :
    private sdbusplus::async::context_ref,
    sdbusplus::async::details::context_friend
{
  public:
    /**
     * @brief Constructor
     *
     * @param[in] ctx - The async context object
     * @param[in] tracker - WaitTracker for tracking timer waits
     * @param[in] callback - Function to call on expiration
     */
    Timer(sdbusplus::async::context& ctx, WaitTracker& tracker,
          std::function<void()>&& callback) :
        context_ref(ctx), waitTracker(tracker), callback(std::move(callback))
    {}

    /**
     * @brief Called by sd-event when time expires. Invokes the
     *        chosen callback function.
     */
    static int handler([[maybe_unused]] sd_event_source* s,
                       [[maybe_unused]] uint64_t usec, void* userdata)
    {
        Timer* t = static_cast<Timer*>(userdata);
        t->callback();
        t->source.reset();
        t->waitGuard.reset();
        return 0;
    }

    /**
     * @brief Starts the timer
     *
     * @param[in] timeout - The timeout value
     * @param[in] operation - Wait operation for tracking
     */
    void start(const std::chrono::microseconds& timeout,
               WaitOperation operation)
    {
        source.reset();
        waitGuard.reset();

        auto s = get_event_loop(ctx).add_oneshot_timer(Timer::handler, this,
                                                       timeout);
        source = std::make_unique<Source>(std::move(s));

        auto timeoutSec =
            std::chrono::duration_cast<std::chrono::seconds>(timeout);
        waitGuard = std::make_unique<WaitTracker::WaitGuard>(
            waitTracker, operation, timeoutSec);
    }

    /**
     * @brief Stops the timer
     */
    void stop()
    {
        source.reset();
        waitGuard.reset();
    }

    /**
     * @brief Check if the timer is currently running
     *
     * @return true if timer is running, false otherwise
     */
    bool isRunning() const
    {
        return source != nullptr;
    }

  private:
    /**
     * @brief RAII wrapper for sdbusplus::source
     */
    struct Source
    {
        Source(sdbusplus::event::source&& s) : source(std::move(s)) {}

      private:
        sdbusplus::event::source source;
    };

    /**
     * @brief The wait tracker object
     */
    WaitTracker& waitTracker;

    /**
     * @brief Function to run on timeout
     */
    std::function<void()> callback;

    /**
     * @brief Event source object
     */
    std::unique_ptr<Source> source;

    /**
     * @brief RAII guard for wait tracking
     */
    std::unique_ptr<WaitTracker::WaitGuard> waitGuard;
};

} // namespace rbmc
