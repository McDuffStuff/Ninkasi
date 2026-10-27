#pragma once
// The user currently operating Ninkasi.
//
// Every service call takes a Session so that logs and audit rows record who
// made a change. Today there is only the built-in local user; when login is
// added, the login screen will produce the Session instead.

#include "data/database.h"

#include <cstdint>
#include <string>

namespace ninkasi::app {

struct Session {
    std::int64_t user_id = 0;
    std::string display_name;
};

// Opens a session for the built-in 'local' user created by the first migration.
Session open_local_session(data::Database& db);

} // namespace ninkasi::app
