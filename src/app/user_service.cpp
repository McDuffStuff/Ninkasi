#include "app/user_service.h"

#include "data/audit_log.h"

#include <stdexcept>
#include <string>

namespace ninkasi::app {

namespace {

constexpr std::string_view kEntity = "user";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

Session session_for(const domain::User& user) {
    return Session{user.id, user.name, user.role};
}

} // namespace

UserService::UserService(data::Database& db, PasswordCost cost) : db_(db), repository_(db), cost_(cost) {}

std::vector<domain::User> UserService::people() {
    return repository_.load_all();
}

std::optional<domain::User> UserService::person(std::int64_t id) {
    return repository_.find(id);
}

domain::User UserService::get(std::int64_t id) {
    std::optional<domain::User> user = repository_.find(id);
    if (!user) throw std::invalid_argument("there is no person with id " + std::to_string(id));
    return *user;
}

std::optional<std::string> UserService::stored(std::string_view password) const {
    if (password.empty()) return std::nullopt;
    return hash_password(password, cost_);
}

int UserService::active_count() {
    int count = 0;
    for (const domain::User& user : repository_.load_all()) count += user.active ? 1 : 0;
    return count;
}

void UserService::check_rules(const Session& by) {
    const std::vector<domain::User> everyone = repository_.load_all();
    int active = 0;
    bool admin = false;
    for (const domain::User& user : everyone) {
        if (!user.active) continue;
        ++active;
        admin = admin || user.role == domain::Role::Admin;
    }
    if (!admin) throw std::invalid_argument("Ninkasi needs at least one active admin.");
    if (active < 2) return;
    for (const domain::User& user : everyone) {
        if (!user.active || user.role != domain::Role::Admin || user.has_password) continue;
        const std::string who = user.id == by.user_id ? "yourself" : user.name;
        throw std::invalid_argument("Set a password for " + who +
                                    " first: while more than one person uses Ninkasi, every admin needs one, so "
                                    "no one else can sign in as an admin.");
    }
}

// --- Signing in ------------------------------------------------------------------------

bool UserService::sign_in_needed() {
    int active = 0;
    for (const domain::User& user : repository_.load_all()) {
        if (!user.active) continue;
        if (user.has_password) return true;
        ++active;
    }
    return active > 1;
}

Session UserService::only_person() {
    if (sign_in_needed()) throw std::logic_error("someone has to sign in");
    for (const domain::User& user : repository_.load_all()) {
        if (!user.active) continue;
        repository_.record_sign_in(user.id, domain::now());
        return session_for(user);
    }
    throw std::logic_error("no one is active");
}

std::optional<Session> UserService::sign_in(std::int64_t user_id, std::string_view password) {
    const std::optional<domain::User> user = repository_.find(user_id);
    if (!user || !user->active) return std::nullopt;
    if (const std::optional<std::string> hash = repository_.password_hash(user_id)) {
        if (!verify_password(password, *hash)) return std::nullopt;
    }

    data::Transaction tx(db_);
    repository_.record_sign_in(user_id, domain::now());
    data::record_audit(db_, user_id, "user.sign_in", kEntity, user_id, user->name);
    tx.commit();
    return session_for(*user);
}

std::optional<std::int64_t> UserService::last_signed_in() {
    return repository_.last_signed_in();
}

// --- Managing people -----------------------------------------------------------------------

std::int64_t UserService::add(const Session& by, std::string_view name, domain::Role role, std::string_view password) {
    require_admin(by, "add people");
    const std::string_view trimmed = trim(name);
    if (trimmed.empty()) throw std::invalid_argument("A name is needed.");
    if (repository_.find_by_name(trimmed)) {
        throw std::invalid_argument("There's already someone called " + std::string(trimmed) + ".");
    }
    // A second person is joining, so an admin needs a password from the start.
    if (role == domain::Role::Admin && password.empty()) throw std::invalid_argument("An admin needs a password.");

    const std::optional<std::string> hash = stored(password);  // before the transaction: hashing takes a moment
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(trimmed, role, hash);
    check_rules(by);
    data::record_audit(db_, by.user_id, "user.add", kEntity, id,
                       std::string(trimmed) + " (" + std::string(domain::name(role)) + ")");
    tx.commit();
    return id;
}

void UserService::rename(const Session& by, std::int64_t id, std::string_view name) {
    require_admin(by, "rename people");
    const domain::User user = get(id);
    const std::string_view trimmed = trim(name);
    if (trimmed.empty()) throw std::invalid_argument("A name is needed.");
    if (trimmed == user.name) return;
    const std::optional<std::int64_t> taken = repository_.find_by_name(trimmed);
    if (taken && *taken != id) {
        throw std::invalid_argument("There's already someone called " + std::string(trimmed) + ".");
    }

    data::Transaction tx(db_);
    repository_.set_name(id, trimmed);
    data::record_audit(db_, by.user_id, "user.rename", kEntity, id,
                       "renamed \"" + user.name + "\" to \"" + std::string(trimmed) + "\"");
    tx.commit();
}

void UserService::set_role(const Session& by, std::int64_t id, domain::Role role) {
    require_admin(by, "change people's roles");
    if (id == by.user_id) throw std::invalid_argument("Another admin has to change your role.");
    const domain::User user = get(id);
    if (user.role == role) return;
    if (role == domain::Role::Admin && user.active && !user.has_password && active_count() > 1) {
        throw std::invalid_argument("Set a password for " + user.name + " before making them an admin.");
    }

    data::Transaction tx(db_);
    repository_.set_role(id, role);
    check_rules(by);
    data::record_audit(db_, by.user_id, "user.role", kEntity, id,
                       user.name + ": " + std::string(domain::name(role)));
    tx.commit();
}

void UserService::set_password(const Session& by, std::int64_t id, std::string_view password) {
    require_admin(by, "set people's passwords");
    replace_password(by, get(id), password);
}

void UserService::change_own_password(const Session& by, std::string_view current, std::string_view replacement) {
    const domain::User user = get(by.user_id);
    if (const std::optional<std::string> hash = repository_.password_hash(user.id)) {
        if (!verify_password(current, *hash)) throw std::invalid_argument("That isn't your current password.");
    }
    replace_password(by, user, replacement);
}

void UserService::replace_password(const Session& by, const domain::User& user, std::string_view password) {
    if (password.empty() && user.role == domain::Role::Admin && user.active && active_count() > 1) {
        throw std::invalid_argument("An admin needs a password while more than one person uses Ninkasi.");
    }

    const std::optional<std::string> hash = stored(password);
    data::Transaction tx(db_);
    repository_.set_password_hash(user.id, hash);
    if (!hash) repository_.set_recovery_hash(user.id, std::nullopt);  // nothing left to recover
    check_rules(by);
    data::record_audit(db_, by.user_id, "user.password", kEntity, user.id,
                       user.name + (hash ? ": password set" : ": password removed"));
    tx.commit();
}

std::string UserService::make_recovery_code(const Session& by) {
    const domain::User user = get(by.user_id);
    if (!user.has_password) throw std::invalid_argument("Set a password first; a recovery code replaces a forgotten one.");

    const std::string code = new_recovery_code();
    const std::string hash = hash_password(normalize_recovery_code(code), cost_);
    data::Transaction tx(db_);
    repository_.set_recovery_hash(user.id, hash);
    data::record_audit(db_, by.user_id, "user.recovery_code", kEntity, user.id, user.name + ": new recovery code");
    tx.commit();
    return code;
}

std::optional<PasswordRecovery> UserService::recover(std::int64_t user_id, std::string_view code,
                                                     std::string_view new_password) {
    if (new_password.empty()) throw std::invalid_argument("Choose a new password.");
    const std::optional<domain::User> user = repository_.find(user_id);
    if (!user || !user->active) return std::nullopt;
    const std::optional<std::string> code_hash = repository_.recovery_hash(user_id);
    if (!code_hash || !verify_password(normalize_recovery_code(code), *code_hash)) return std::nullopt;

    // The code is used up: a new one replaces it.
    const std::string new_code = new_recovery_code();
    const std::string new_code_hash = hash_password(normalize_recovery_code(new_code), cost_);
    const std::string password_hash = hash_password(new_password, cost_);
    data::Transaction tx(db_);
    repository_.set_password_hash(user_id, password_hash);
    repository_.set_recovery_hash(user_id, new_code_hash);
    repository_.record_sign_in(user_id, domain::now());
    data::record_audit(db_, user_id, "user.recover", kEntity, user_id,
                       user->name + ": new password chosen with a recovery code");
    tx.commit();
    return PasswordRecovery{session_for(*user), new_code};
}

void UserService::set_active(const Session& by, std::int64_t id, bool active) {
    require_admin(by, active ? "bring people back" : "make people inactive");
    if (id == by.user_id && !active) throw std::invalid_argument("You can't make yourself inactive; another admin can.");
    const domain::User user = get(id);
    if (user.active == active) return;

    data::Transaction tx(db_);
    repository_.set_active(id, active);
    check_rules(by);
    data::record_audit(db_, by.user_id, active ? "user.reactivate" : "user.deactivate", kEntity, id, user.name);
    tx.commit();
}

} // namespace ninkasi::app
