// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>

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
     * @param[in] callback - Function to call on expiration
     */
    Timer(sdbusplus::async::context& ctx, std::function<void()>&& callback) :
        context_ref(ctx), callback(std::move(callback))
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
        return 0;
    }

    /**
     * @brief Starts the timer
     *
     * @param[in] timeout - The timeout value
     */
    void start(const std::chrono::microseconds& timeout)
    {
        source.reset();

        auto s = get_event_loop(ctx).add_oneshot_timer(Timer::handler, this,
                                                       timeout);
        source = std::make_unique<Source>(std::move(s));
    }

    /**
     * @brief Stops the timer
     */
    void stop()
    {
        source.reset();
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
     * @brief Function to run on timeout
     */
    std::function<void()> callback;

    /**
     * @brief Event source object
     */
    std::unique_ptr<Source> source;
};

} // namespace rbmc
