#pragma once
// Reads and writes tasks.

#include "data/database.h"
#include "domain/task.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ninkasi::data {

class TaskRepository {
public:
    explicit TaskRepository(Database& db) : db_(db) {}

    // Open tasks: dated ones by due date, then undated ones, oldest first.
    std::vector<domain::Task> open_tasks();
    // Done tasks, most recently done first. With an element, only its tasks.
    std::vector<domain::Task> done_tasks(std::optional<std::int64_t> element_id, int limit);
    std::optional<domain::Task> load(std::int64_t id);

    // Inserts the task (its id is ignored) and returns the new id.
    std::int64_t insert(const domain::Task& task, std::int64_t user_id);
    // Saves title, notes, element, batch and due date.
    void update(const domain::Task& task);
    void set_done(std::int64_t id, std::optional<domain::Timestamp> done_at, std::int64_t user_id);
    void remove(std::int64_t id);

private:
    Database& db_;
};

} // namespace ninkasi::data
