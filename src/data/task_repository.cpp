#include "data/task_repository.h"

#include <string>

namespace ninkasi::data {

namespace {

constexpr const char* kColumns = "id, title, notes, element_id, batch_id, due_date, done_at";

domain::Task read_task(const Statement& stmt) {
    domain::Task task;
    task.id = stmt.column_int64(0);
    task.title = stmt.column_text(1);
    task.notes = stmt.column_text(2);
    task.element_id = stmt.column_optional_int64(3);
    task.batch_id = stmt.column_optional_int64(4);
    if (!stmt.column_is_null(5)) task.due = domain::parse_date(stmt.column_text(5));
    if (!stmt.column_is_null(6)) task.done_at = domain::parse_iso8601(stmt.column_text(6));
    return task;
}

void bind_due(Statement& stmt, int index, const std::optional<domain::Date>& due) {
    if (due) {
        stmt.bind(index, domain::to_iso8601(*due));
    } else {
        stmt.bind_null(index);
    }
}

} // namespace

std::vector<domain::Task> TaskRepository::open_tasks() {
    std::vector<domain::Task> tasks;
    auto stmt = db_.prepare(std::string("SELECT ") + kColumns +
                            " FROM tasks WHERE done_at IS NULL ORDER BY due_date IS NULL, due_date, id;");
    while (stmt.step()) tasks.push_back(read_task(stmt));
    return tasks;
}

std::vector<domain::Task> TaskRepository::done_tasks(std::optional<std::int64_t> element_id, int limit) {
    std::vector<domain::Task> tasks;
    auto stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM tasks WHERE done_at IS NOT NULL" +
                            (element_id ? " AND element_id = ?2" : "") + " ORDER BY done_at DESC, id DESC LIMIT ?1;");
    stmt.bind(1, limit);
    if (element_id) stmt.bind(2, *element_id);
    while (stmt.step()) tasks.push_back(read_task(stmt));
    return tasks;
}

std::optional<domain::Task> TaskRepository::load(std::int64_t id) {
    auto stmt = db_.prepare(std::string("SELECT ") + kColumns + " FROM tasks WHERE id = ?;");
    stmt.bind(1, id);
    if (!stmt.step()) return std::nullopt;
    return read_task(stmt);
}

std::int64_t TaskRepository::insert(const domain::Task& task, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO tasks (title, notes, element_id, batch_id, due_date, created_by) VALUES (?, ?, ?, ?, ?, ?);");
    stmt.bind(1, task.title).bind(2, task.notes);
    stmt.bind_optional(3, task.element_id);
    stmt.bind_optional(4, task.batch_id);
    bind_due(stmt, 5, task.due);
    stmt.bind(6, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

void TaskRepository::update(const domain::Task& task) {
    auto stmt = db_.prepare(
        "UPDATE tasks SET title = ?, notes = ?, element_id = ?, batch_id = ?, due_date = ? WHERE id = ?;");
    stmt.bind(1, task.title).bind(2, task.notes);
    stmt.bind_optional(3, task.element_id);
    stmt.bind_optional(4, task.batch_id);
    bind_due(stmt, 5, task.due);
    stmt.bind(6, task.id);
    stmt.execute();
}

void TaskRepository::set_done(std::int64_t id, std::optional<domain::Timestamp> done_at, std::int64_t user_id) {
    auto stmt = db_.prepare("UPDATE tasks SET done_at = ?, done_by = ? WHERE id = ?;");
    if (done_at) {
        stmt.bind(1, domain::to_iso8601(*done_at)).bind(2, user_id);
    } else {
        stmt.bind_null(1).bind_null(2);
    }
    stmt.bind(3, id);
    stmt.execute();
}

void TaskRepository::remove(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM tasks WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

} // namespace ninkasi::data
