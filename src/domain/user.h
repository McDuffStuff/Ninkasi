#pragma once
// The people who use Ninkasi, and what their role lets them do.

#include "domain/timestamp.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ninkasi::domain {

// Admins can do everything. Brewers log the brewery's work (brews, transfers,
// readings, inventory, packaging, tasks, recipes) but don't manage people,
// restore backups, change the brewery's settings (units, brewing settings,
// TTB and backup settings) or set up equipment on the floor.
enum class Role { Admin, Brewer };

inline constexpr Role kAllRoles[] = {Role::Admin, Role::Brewer};

struct User {
    std::int64_t id = 0;
    std::string name;
    Role role = Role::Brewer;
    bool has_password = false;
    // Someone who leaves is made inactive rather than deleted, so the records
    // they made keep their name. Inactive people can't sign in.
    bool active = true;
    std::optional<Timestamp> last_sign_in;
};

std::string_view name(Role role);  // "Admin", "Brewer"
std::string_view key(Role role);   // stored in the database: never change once shipped
std::optional<Role> role_from_key(std::string_view text);

} // namespace ninkasi::domain
