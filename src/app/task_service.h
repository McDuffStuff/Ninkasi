#pragma once
// Tasks: things to do in the brewery, optionally for a piece of equipment or
// a batch, and optionally by a date.
//
// Open tasks are kept in memory for the floor's badges. Completing a task is
// written to the activity log; adding, editing and deleting are audited.
// Removing a piece of equipment deletes its tasks: call refresh() after.

#include "app/session.h"
#include "data/database.h"
#include "data/task_repository.h"
#include "domain/task.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ninkasi::app {

class TaskService {
public:
    TaskService(data::Database& db, Session session);

    // Dated tasks by due date, then undated ones, oldest first.
    const std::vector<domain::Task>& open_tasks() const { return open_; }
    // An element's open tasks, in the same order.
    std::vector<domain::Task> open_for(std::int64_t element_id) const;
    // The most pressing of an element's open tasks.
    domain::TaskUrgency urgency_for(std::int64_t element_id, domain::Date today) const;
    // Grows with every change, so windows know when to re-read the database.
    std::uint64_t revision() const { return revision_; }

    // Done tasks, most recent first; with an element, only its tasks.
    std::vector<domain::Task> recently_done(std::optional<std::int64_t> element_id, int limit = 50);

    // Each throws std::invalid_argument if the title is blank.
    std::int64_t add(const domain::Task& task);
    void update(const domain::Task& task);

    void complete(std::int64_t id, domain::Timestamp at);
    void reopen(std::int64_t id);
    void remove(std::int64_t id);

    // Re-reads the open tasks.
    void refresh();

private:
    data::Database& db_;
    Session session_;
    data::TaskRepository repository_;
    std::vector<domain::Task> open_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
