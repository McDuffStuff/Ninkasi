#include "app/session.h"

namespace ninkasi::app {

void require_admin(const Session& session, std::string_view what) {
    if (!session.is_admin()) throw NotAllowed("Only an admin can " + std::string(what) + ".");
}

Session open_local_session(data::Database& db) {
    auto stmt = db.prepare("SELECT id, display_name, role FROM users ORDER BY id LIMIT 1;");
    if (!stmt.step()) throw data::DbError("the brewery's first user is missing from the database");
    return Session{stmt.column_int64(0), stmt.column_text(1),
                   domain::role_from_key(stmt.column_text(2)).value_or(domain::Role::Brewer)};
}

} // namespace ninkasi::app
