#pragma once
// The people who use Ninkasi: signing in, and (for admins) adding people,
// changing their names, roles and passwords, and making them inactive when
// they leave.
//
// The rules, checked on every change:
// - There is always at least one active admin.
// - While more than one person is active, every active admin has a password;
//   otherwise anyone could sign in as an admin. A one-person brewery needs no
//   password, and opens without signing in.
// - Brewers' passwords are optional.
// - Names are unique, ignoring case.
// - No one changes their own role or makes themselves inactive; another
//   admin does that.
//
// Someone who forgets their password can choose a new one at the sign-in
// screen with their recovery code (or ask an admin to set one). A code is
// made when someone first sets their own password, shown to them once, and
// stored hashed like a password. Using it uses it up, and a new one is made.
//
// Passwords protect who does what in Ninkasi. They don't encrypt the
// database: anyone who can open the file can read it.

#include "app/password.h"
#include "app/session.h"
#include "data/database.h"
#include "data/user_repository.h"
#include "domain/user.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

// A password recovered with a recovery code.
struct PasswordRecovery {
    Session session;       // signed in
    std::string new_code;  // replaces the code used up, to show them
};

class UserService {
public:
    // `cost` is how hard new password hashes are to compute (tests use a cheap one).
    explicit UserService(data::Database& db, PasswordCost cost = {});

    // Everyone: active people first, then by name.
    std::vector<domain::User> people();
    std::optional<domain::User> person(std::int64_t id);

    // --- Signing in -------------------------------------------------------------------
    // Whether people say who they are when Ninkasi starts: once more than one
    // person is active, or anyone has a password.
    bool sign_in_needed();
    // The only active person's session, for when no sign-in is needed.
    // Throws std::logic_error if sign-in is needed.
    Session only_person();
    // `user_id`'s session if they're active and `password` is theirs (anything
    // will do for someone without a password); nullopt otherwise. Records
    // when they signed in.
    std::optional<Session> sign_in(std::int64_t user_id, std::string_view password);
    // Who signed in last on this database, to suggest on the sign-in screen.
    std::optional<std::int64_t> last_signed_in();

    // --- Managing people (admins only; others get NotAllowed) ----------------------------
    // Each throws std::invalid_argument, changing nothing, if the change breaks
    // a rule above. An empty password means none.
    std::int64_t add(const Session& by, std::string_view name, domain::Role role, std::string_view password);
    void rename(const Session& by, std::int64_t id, std::string_view name);
    void set_role(const Session& by, std::int64_t id, domain::Role role);
    void set_password(const Session& by, std::int64_t id, std::string_view password);
    void set_active(const Session& by, std::int64_t id, bool active);

    // --- Anyone ---------------------------------------------------------------------------
    // Changes the signed-in person's own password. Throws std::invalid_argument
    // if `current` isn't their password, or an admin who must have a password
    // tries to remove it. Removing a password removes the recovery code too.
    void change_own_password(const Session& by, std::string_view current, std::string_view replacement);

    // Makes `by` a new recovery code, replacing any they had, and returns it to
    // show them. Throws std::invalid_argument if they have no password.
    std::string make_recovery_code(const Session& by);

    // At the sign-in screen: if `code` is `user_id`'s recovery code, sets their
    // password to `new_password`, signs them in and makes them a new code.
    // nullopt, changing nothing, if the code is wrong or they're inactive.
    // Throws std::invalid_argument if `new_password` is empty.
    std::optional<PasswordRecovery> recover(std::int64_t user_id, std::string_view code,
                                            std::string_view new_password);

private:
    // Throws std::invalid_argument if there's no such person.
    domain::User get(std::int64_t id);
    // The password to store: a hash, or nullopt for none.
    std::optional<std::string> stored(std::string_view password) const;
    int active_count();
    // Sets (or, if empty, removes) `user`'s password, by the rules.
    void replace_password(const Session& by, const domain::User& user, std::string_view password);
    // Throws std::invalid_argument naming the broken rule. Run inside the
    // change's transaction, after the change, so a broken rule rolls it back.
    void check_rules(const Session& by);

    data::Database& db_;
    data::UserRepository repository_;
    PasswordCost cost_;
};

} // namespace ninkasi::app
