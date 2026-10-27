#include "data/settings_repository.h"

namespace ninkasi::data {

std::optional<std::string> SettingsRepository::get(std::string_view key) {
    auto stmt = db_.prepare("SELECT value FROM settings WHERE key = ?;");
    stmt.bind(1, key);
    if (!stmt.step()) return std::nullopt;
    return stmt.column_text(0);
}

std::string SettingsRepository::get_or(std::string_view key, std::string_view fallback) {
    return get(key).value_or(std::string(fallback));
}

void SettingsRepository::set(std::string_view key, std::string_view value) {
    auto stmt = db_.prepare(
        "INSERT INTO settings (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    stmt.bind(1, key).bind(2, value);
    stmt.execute();
}

} // namespace ninkasi::data
