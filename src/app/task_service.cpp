#include "app/task_service.h"

#include "data/activity_log.h"
#include "data/audit_log.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr std::string_view kEntity = "task";

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// The task with its title trimmed; throws if that leaves nothing.
domain::Task checked(domain::Task task) {
    task.title = trimmed(task.title);
    if (task.title.empty()) throw std::invalid_argument("Give the task a title.");
    return task;
}

} // namespace

TaskService::TaskService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {
    refresh();
}

std::vector<domain::Task> TaskService::open_for(std::int64_t element_id) const {
    std::vector<domain::Task> tasks;
    for (const domain::Task& task : open_) {
        if (task.element_id == element_id) tasks.push_back(task);
    }
    return tasks;
}

domain::TaskUrgency TaskService::urgency_for(std::int64_t element_id, domain::Date today) const {
    domain::TaskUrgency most = domain::TaskUrgency::None;
    for (const domain::Task& task : open_) {
        if (task.element_id == element_id) most = std::max(most, domain::urgency(task, today));
    }
    return most;
}

std::vector<domain::Task> TaskService::recently_done(std::optional<std::int64_t> element_id, int limit) {
    return repository_.done_tasks(element_id, limit);
}

std::int64_t TaskService::add(const domain::Task& task) {
    const domain::Task saved = checked(task);
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(saved, session_.user_id);
    data::record_audit(db_, session_.user_id, "task.create", kEntity, id, "\"" + saved.title + "\"");
    tx.commit();
    refresh();
    return id;
}

void TaskService::update(const domain::Task& task) {
    const domain::Task saved = checked(task);
    data::Transaction tx(db_);
    repository_.update(saved);
    data::record_audit(db_, session_.user_id, "task.update", kEntity, saved.id, "\"" + saved.title + "\"");
    tx.commit();
    refresh();
}

void TaskService::complete(std::int64_t id, domain::Timestamp at) {
    const std::optional<domain::Task> task = repository_.load(id);
    if (!task || task->done()) return;

    domain::Activity entry;
    entry.at = at;
    entry.kind = "task";
    entry.batch_id = task->batch_id;
    entry.element_id = task->element_id;
    entry.text = "Done: " + task->title;

    data::Transaction tx(db_);
    repository_.set_done(id, at, session_.user_id);
    data::record_activity(db_, session_.user_id, entry);
    tx.commit();
    refresh();
}

void TaskService::reopen(std::int64_t id) {
    const std::optional<domain::Task> task = repository_.load(id);
    if (!task || !task->done()) return;
    data::Transaction tx(db_);
    repository_.set_done(id, std::nullopt, session_.user_id);
    data::record_audit(db_, session_.user_id, "task.reopen", kEntity, id, "\"" + task->title + "\"");
    tx.commit();
    refresh();
}

void TaskService::remove(std::int64_t id) {
    const std::optional<domain::Task> task = repository_.load(id);
    if (!task) return;
    data::Transaction tx(db_);
    repository_.remove(id);
    data::record_audit(db_, session_.user_id, "task.delete", kEntity, id, "\"" + task->title + "\"");
    tx.commit();
    refresh();
}

void TaskService::refresh() {
    open_ = repository_.open_tasks();
    ++revision_;
}

} // namespace ninkasi::app
