#include "data/packaging_repository.h"

namespace ninkasi::data {

namespace {

domain::Timestamp column_time(const Statement& stmt, int col) {
    return domain::parse_iso8601(stmt.column_text(col)).value_or(domain::Timestamp{});
}

} // namespace

// --- Kegs ---------------------------------------------------------------------

void PackagingRepository::insert_keg_movement(const domain::KegMovement& movement, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO keg_movements (at, keg_type, state, quantity, reason, batch_id, note, user_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(movement.at)).bind(2, domain::key(movement.type));
    stmt.bind(3, domain::key(movement.state)).bind(4, movement.quantity).bind(5, domain::key(movement.reason));
    stmt.bind_optional(6, movement.batch_id);
    stmt.bind(7, movement.note).bind(8, user_id);
    stmt.execute();
}

domain::KegCounts PackagingRepository::keg_counts(domain::PackageType type) {
    auto stmt = db_.prepare(
        "SELECT COALESCE(SUM(CASE WHEN state = 'dirty' THEN quantity END), 0), "
        "COALESCE(SUM(CASE WHEN state = 'clean' THEN quantity END), 0) FROM keg_movements WHERE keg_type = ?;");
    stmt.bind(1, domain::key(type));
    stmt.step();
    return {static_cast<int>(stmt.column_int64(0)), static_cast<int>(stmt.column_int64(1))};
}

std::vector<KegMovementRow> PackagingRepository::keg_movements(int limit) {
    auto stmt = db_.prepare(
        "SELECT k.id, k.at, k.keg_type, k.state, k.quantity, k.reason, k.batch_id, k.note, u.display_name, "
        "COALESCE(b.code, '') FROM keg_movements k JOIN users u ON u.id = k.user_id "
        "LEFT JOIN batches b ON b.id = k.batch_id ORDER BY k.at DESC, k.id DESC LIMIT ?;");
    stmt.bind(1, limit);
    std::vector<KegMovementRow> rows;
    while (stmt.step()) {
        KegMovementRow row;
        domain::KegMovement& m = row.movement;
        m.id = stmt.column_int64(0);
        m.at = column_time(stmt, 1);
        m.type = domain::package_type_from_key(stmt.column_text(2)).value_or(domain::PackageType::HalfBarrel);
        m.state = domain::keg_state_from_key(stmt.column_text(3)).value_or(domain::KegState::Dirty);
        m.quantity = static_cast<int>(stmt.column_int64(4));
        m.reason = domain::keg_reason_from_key(stmt.column_text(5)).value_or(domain::KegReason::Added);
        m.batch_id = stmt.column_optional_int64(6);
        m.note = stmt.column_text(7);
        m.user = stmt.column_text(8);
        row.batch_code = stmt.column_text(9);
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace ninkasi::data
