#pragma once
// Tasks: things to do in the brewery, optionally for a piece of equipment or
// a batch, and optionally by a date.

#include "domain/timestamp.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ninkasi::domain {

struct Task {
    std::int64_t id = 0;
    std::string title;
    std::string notes;
    std::optional<std::int64_t> element_id;
    std::optional<std::int64_t> batch_id;
    std::optional<Date> due;
    std::optional<Timestamp> done_at;

    bool done() const { return done_at.has_value(); }
};

enum class TaskUrgency {
    None,      // done, or no due date
    Upcoming,
    DueToday,
    Overdue,
};

inline TaskUrgency urgency(const Task& task, Date today) {
    if (task.done() || !task.due) return TaskUrgency::None;
    if (*task.due < today) return TaskUrgency::Overdue;
    if (*task.due == today) return TaskUrgency::DueToday;
    return TaskUrgency::Upcoming;
}

} // namespace ninkasi::domain
