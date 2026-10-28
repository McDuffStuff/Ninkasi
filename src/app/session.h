#pragma once
// The person currently using Ninkasi.
//
// Every service call takes a Session, so logs and audit rows record who made
// a change and the services can refuse what the person's role doesn't allow.
// Signing in (UserService) produces it.

#include "data/database.h"
#include "domain/user.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ninkasi::app {

struct Session {
    std::int64_t user_id = 0;
    std::string display_name;
    domain::Role role = domain::Role::Brewer;

    bool is_admin() const { return role == domain::Role::Admin; }
};

// Thrown when someone tries something their role doesn't allow. The UI hides
// those actions, so this is a safeguard rather than something people see.
class NotAllowed : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Throws NotAllowed ("Only an admin can <what>.") unless `session` is an admin's.
void require_admin(const Session& session, std::string_view what);

// Opens a session for the brewery's first person, the admin the first
// migration created. For tests; the app signs people in through UserService.
Session open_local_session(data::Database& db);

} // namespace ninkasi::app
