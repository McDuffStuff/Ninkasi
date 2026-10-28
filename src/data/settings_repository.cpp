#include "data/settings_repository.h"

namespace ninkasi::data {

std::optional<std::string> SettingsRepository::get(std::string_view key) {
    auto stmt = user_id_ ? db_.prepare("SELECT value FROM user_settings WHERE user_id = ? AND key = ?;")
                         : db_.prepare("SELECT value FROM settings WHERE key = ?;");
    int next = 1;
    if (user_id_) stmt.bind(next++, *user_id_);
    stmt.bind(next, key);
    if (!stmt.step()) return std::nullopt;
    return stmt.column_text(0);
}

std::string SettingsRepository::get_or(std::string_view key, std::string_view fallback) {
    return get(key).value_or(std::string(fallback));
}

void SettingsRepository::set(std::string_view key, std::string_view value) {
    auto stmt = user_id_ ? db_.prepare("INSERT INTO user_settings (user_id, key, value) VALUES (?, ?, ?) "
                                       "ON CONFLICT(user_id, key) DO UPDATE SET value = excluded.value;")
                         : db_.prepare("INSERT INTO settings (key, value) VALUES (?, ?) "
                                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    int next = 1;
    if (user_id_) stmt.bind(next++, *user_id_);
    stmt.bind(next, key).bind(next + 1, value);
    stmt.execute();
}

} // namespace ninkasi::data
