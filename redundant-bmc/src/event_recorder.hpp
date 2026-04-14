// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"

#include <set>

namespace rbmc
{

/**
 * @class EventRecorder
 *
 * Holds a list of interesting events, mostly for testing.
 */
class EventRecorder
{
  public:
    EventRecorder() = default;
    ~EventRecorder() = default;

    EventRecorder(const EventRecorder&) = delete;
    EventRecorder& operator=(const EventRecorder&) = delete;
    EventRecorder(EventRecorder&&) = delete;
    EventRecorder& operator=(EventRecorder&&) = delete;

    /**
     * @brief Record an event
     *
     * @param[in] event - The event to record
     */
    void record(Event event)
    {
        events.insert(event);
    }

    /**
     * @brief Check if an event has occurred
     *
     * @param[in] event - The event to check
     * @return true if event has been recorded
     */
    bool hasEvent(Event event) const
    {
        return events.contains(event);
    }

  private:
    /**
     * @brief Set of events that have occurred
     */
    std::set<Event> events;
};

} // namespace rbmc
