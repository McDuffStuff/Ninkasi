#include "data/activity_log.h"

#include <string>

namespace ninkasi::data {

namespace {

std::vector<domain::Activity> query(Database& db, const std::string& where, std::int64_t id, int limit) {
    std::vector<domain::Activity> entries;
    auto stmt = db.prepare(
        "SELECT a.id, a.at, u.display_name, a.kind, a.batch_id, a.element_id, a.other_element_id, a.text "
        "FROM activity_log a JOIN users u ON u.id = a.user_id WHERE " +
        where + " ORDER BY a.at DESC, a.id DESC LIMIT ?;");
    stmt.bind(1, id).bind(2, limit);
    while (stmt.step()) {
        domain::Activity entry;
        entry.id = stmt.column_int64(0);
        entry.at = domain::parse_iso8601(stmt.column_text(1)).value_or(domain::Timestamp{});
        entry.user = stmt.column_text(2);
        entry.kind = stmt.column_text(3);
        entry.batch_id = stmt.column_optional_int64(4);
        entry.element_id = stmt.column_optional_int64(5);
        entry.other_element_id = stmt.column_optional_int64(6);
        entry.text = stmt.column_text(7);
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace

void record_activity(Database& db, std::int64_t user_id, const domain::Activity& entry) {
    auto stmt = db.prepare(
        "INSERT INTO activity_log (at, user_id, kind, batch_id, element_id, other_element_id, text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(entry.at)).bind(2, user_id).bind(3, entry.kind);
    stmt.bind_optional(4, entry.batch_id);
    stmt.bind_optional(5, entry.element_id);
    stmt.bind_optional(6, entry.other_element_id);
    stmt.bind(7, entry.text);
    stmt.execute();
}

std::vector<domain::Activity> activity_for_batch(Database& db, std::int64_t batch_id, int limit) {
    return query(db, "a.batch_id = ?1", batch_id, limit);
}

std::vector<domain::Activity> activity_for_element(Database& db, std::int64_t element_id, int limit) {
    return query(db, "(a.element_id = ?1 OR a.other_element_id = ?1)", element_id, limit);
}

} // namespace ninkasi::data
