#include "data/user_repository.h"

#include <utility>

namespace ninkasi::data {

namespace {

constexpr const char* kColumns = "SELECT id, display_name, role, password_hash IS NOT NULL, active, last_sign_in "
                                 "FROM users ";

// The name as stored in `username`: lower case, for comparing names.
std::string username_for(std::string_view name) {
    std::string lowered(name);
    for (char& c : lowered) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return lowered;
}

domain::User read_user(const Statement& stmt) {
    domain::User user;
    user.id = stmt.column_int64(0);
    user.name = stmt.column_text(1);
    user.role = domain::role_from_key(stmt.column_text(2)).value_or(domain::Role::Brewer);
    user.has_password = stmt.column_int64(3) != 0;
    user.active = stmt.column_int64(4) != 0;
    if (!stmt.column_is_null(5)) user.last_sign_in = domain::parse_iso8601(stmt.column_text(5));
    return user;
}

void bind_hash(Statement& stmt, int index, const std::optional<std::string>& hash) {
    if (hash) {
        stmt.bind(index, *hash);
    } else {
        stmt.bind_null(index);
    }
}

} // namespace

std::vector<domain::User> UserRepository::load_all() {
    std::vector<domain::User> users;
    auto stmt = db_.prepare(std::string(kColumns) + "ORDER BY active DESC, display_name COLLATE NOCASE, id;");
    while (stmt.step()) users.push_back(read_user(stmt));
    return users;
}

std::optional<domain::User> UserRepository::find(std::int64_t id) {
    auto stmt = db_.prepare(std::string(kColumns) + "WHERE id = ?;");
    stmt.bind(1, id);
    if (!stmt.step()) return std::nullopt;
    return read_user(stmt);
}

std::optional<std::int64_t> UserRepository::find_by_name(std::string_view name) {
    auto stmt = db_.prepare("SELECT id FROM users WHERE username = ?;");
    stmt.bind(1, username_for(name));
    if (!stmt.step()) return std::nullopt;
    return stmt.column_int64(0);
}

std::optional<std::string> UserRepository::password_hash(std::int64_t id) {
    auto stmt = db_.prepare("SELECT password_hash FROM users WHERE id = ?;");
    stmt.bind(1, id);
    if (!stmt.step() || stmt.column_is_null(0)) return std::nullopt;
    return stmt.column_text(0);
}

std::optional<std::int64_t> UserRepository::last_signed_in() {
    auto stmt = db_.prepare(
        "SELECT id FROM users WHERE last_sign_in IS NOT NULL ORDER BY last_sign_in DESC, id DESC LIMIT 1;");
    if (!stmt.step()) return std::nullopt;
    return stmt.column_int64(0);
}

std::int64_t UserRepository::insert(std::string_view name, domain::Role role,
                                    const std::optional<std::string>& password_hash) {
    auto stmt = db_.prepare("INSERT INTO users (username, display_name, role, password_hash) VALUES (?, ?, ?, ?);");
    stmt.bind(1, username_for(name)).bind(2, name).bind(3, domain::key(role));
    bind_hash(stmt, 4, password_hash);
    stmt.execute();
    return db_.last_insert_id();
}

void UserRepository::set_name(std::int64_t id, std::string_view name) {
    auto stmt = db_.prepare("UPDATE users SET username = ?, display_name = ? WHERE id = ?;");
    stmt.bind(1, username_for(name)).bind(2, name).bind(3, id);
    stmt.execute();
}

void UserRepository::set_role(std::int64_t id, domain::Role role) {
    auto stmt = db_.prepare("UPDATE users SET role = ? WHERE id = ?;");
    stmt.bind(1, domain::key(role)).bind(2, id);
    stmt.execute();
}

void UserRepository::set_password_hash(std::int64_t id, const std::optional<std::string>& password_hash) {
    auto stmt = db_.prepare("UPDATE users SET password_hash = ? WHERE id = ?;");
    bind_hash(stmt, 1, password_hash);
    stmt.bind(2, id);
    stmt.execute();
}

void UserRepository::set_active(std::int64_t id, bool active) {
    auto stmt = db_.prepare("UPDATE users SET active = ? WHERE id = ?;");
    stmt.bind(1, active ? 1 : 0).bind(2, id);
    stmt.execute();
}

void UserRepository::record_sign_in(std::int64_t id, domain::Timestamp at) {
    auto stmt = db_.prepare("UPDATE users SET last_sign_in = ? WHERE id = ?;");
    stmt.bind(1, domain::to_iso8601(at)).bind(2, id);
    stmt.execute();
}

} // namespace ninkasi::data
