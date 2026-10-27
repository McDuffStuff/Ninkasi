#include "data/audit_log.h"

namespace ninkasi::data {

void record_audit(Database& db, std::int64_t user_id, std::string_view action, std::string_view entity,
                  std::int64_t entity_id, std::string_view details) {
    auto stmt = db.prepare(
        "INSERT INTO audit_log (user_id, action, entity, entity_id, details) VALUES (?, ?, ?, ?, ?);");
    stmt.bind(1, user_id).bind(2, action).bind(3, entity).bind(4, entity_id).bind(5, details);
    stmt.execute();
}

} // namespace ninkasi::data
