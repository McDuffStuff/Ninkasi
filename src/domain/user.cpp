#include "domain/user.h"

namespace ninkasi::domain {

std::string_view name(Role role) {
    switch (role) {
    case Role::Admin: return "Admin";
    case Role::Brewer: return "Brewer";
    }
    return "Brewer";
}

std::string_view key(Role role) {
    switch (role) {
    case Role::Admin: return "admin";
    case Role::Brewer: return "brewer";
    }
    return "brewer";
}

std::optional<Role> role_from_key(std::string_view text) {
    for (Role role : kAllRoles) {
        if (key(role) == text) return role;
    }
    return std::nullopt;
}

} // namespace ninkasi::domain
