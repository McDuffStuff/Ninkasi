#pragma once
// Reads and writes the people who use Ninkasi (the `users` table).
//
// `username` holds the name in lower case (ASCII letters), so the table's
// UNIQUE constraint keeps names unique whatever their case.

#include "data/database.h"
#include "domain/user.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::data {

class UserRepository {
public:
    explicit UserRepository(Database& db) : db_(db) {}

    // Everyone: active people first, then by name.
    std::vector<domain::User> load_all();
    std::optional<domain::User> find(std::int64_t id);
    // Whoever is called `name`, ignoring case.
    std::optional<std::int64_t> find_by_name(std::string_view name);
    // nullopt if the person has no password.
    std::optional<std::string> password_hash(std::int64_t id);
    // Whoever signed in most recently, if anyone has.
    std::optional<std::int64_t> last_signed_in();

    std::int64_t insert(std::string_view name, domain::Role role, const std::optional<std::string>& password_hash);
    void set_name(std::int64_t id, std::string_view name);
    void set_role(std::int64_t id, domain::Role role);
    void set_password_hash(std::int64_t id, const std::optional<std::string>& password_hash);
    void set_active(std::int64_t id, bool active);
    void record_sign_in(std::int64_t id, domain::Timestamp at);

private:
    Database& db_;
};

} // namespace ninkasi::data
