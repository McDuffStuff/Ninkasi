#include "app/session.h"

namespace ninkasi::app {

Session open_local_session(data::Database& db) {
    auto stmt = db.prepare("SELECT id, display_name FROM users WHERE username = 'local';");
    if (!stmt.step()) throw data::DbError("the local user is missing from the database");
    return Session{stmt.column_int64(0), stmt.column_text(1)};
}

} // namespace ninkasi::app
