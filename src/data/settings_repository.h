#pragma once
// Reads and writes key/value settings: the brewery's (the `settings` table,
// shared by everyone) or one person's own (`user_settings`: their theme,
// window layout, which windows are open...).

#include "data/database.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ninkasi::data {

class SettingsRepository {
public:
    // The brewery's settings.
    explicit SettingsRepository(Database& db) : db_(db) {}
    // `user_id`'s own settings.
    SettingsRepository(Database& db, std::int64_t user_id) : db_(db), user_id_(user_id) {}

    std::optional<std::string> get(std::string_view key);
    std::string get_or(std::string_view key, std::string_view fallback);
    void set(std::string_view key, std::string_view value);

private:
    Database& db_;
    std::optional<std::int64_t> user_id_;  // nullopt: the brewery's
};

} // namespace ninkasi::data
