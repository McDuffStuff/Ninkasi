#pragma once
// Reads and writes rows in the `settings` key/value table.

#include "data/database.h"

#include <optional>
#include <string>
#include <string_view>

namespace ninkasi::data {

class SettingsRepository {
public:
    explicit SettingsRepository(Database& db) : db_(db) {}

    std::optional<std::string> get(std::string_view key);
    std::string get_or(std::string_view key, std::string_view fallback);
    void set(std::string_view key, std::string_view value);

private:
    Database& db_;
};

} // namespace ninkasi::data
