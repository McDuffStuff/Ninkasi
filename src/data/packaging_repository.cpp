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

// --- Packaged beer ------------------------------------------------------------

void PackagingRepository::insert_packaged_movement(const domain::PackagedMovement& movement,
                                                   std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO packaged_movements (at, batch_id, package, unit_volume_l, quantity, reason, vessel_id, note, "
        "user_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(movement.at)).bind(2, movement.batch_id).bind(3, domain::key(movement.type));
    stmt.bind(4, movement.unit_volume_l).bind(5, movement.quantity).bind(6, domain::key(movement.reason));
    stmt.bind_optional(7, movement.vessel_id);
    stmt.bind(8, movement.note).bind(9, user_id);
    stmt.execute();
}

std::vector<domain::PackagedStock> PackagingRepository::packaged_stock() {
    auto stmt = db_.prepare(
        "SELECT p.batch_id, b.code, b.name, p.package, p.unit_volume_l, SUM(p.quantity), MIN(p.at) "
        "FROM packaged_movements p JOIN batches b ON b.id = p.batch_id "
        "GROUP BY p.batch_id, p.package, p.unit_volume_l HAVING SUM(p.quantity) <> 0 "
        // Each beer's packages together, beers in the order they were first
        // packaged; kegs, then cans.
        "ORDER BY (SELECT MIN(at) FROM packaged_movements WHERE batch_id = p.batch_id), p.batch_id, "
        "CASE p.package WHEN 'half_barrel' THEN 0 WHEN 'sixtel' THEN 1 ELSE 2 END, p.unit_volume_l;");
    std::vector<domain::PackagedStock> stock;
    while (stmt.step()) {
        domain::PackagedStock entry;
        entry.batch_id = stmt.column_int64(0);
        entry.batch_code = stmt.column_text(1);
        entry.beer = stmt.column_text(2);
        entry.type = domain::package_type_from_key(stmt.column_text(3)).value_or(domain::PackageType::HalfBarrel);
        entry.unit_volume_l = stmt.column_double(4);
        entry.count = static_cast<int>(stmt.column_int64(5));
        entry.packaged_at = column_time(stmt, 6);
        stock.push_back(std::move(entry));
    }
    return stock;
}

std::vector<domain::PackagedStock> PackagingRepository::packaged_ever(int limit) {
    auto stmt = db_.prepare(
        "SELECT p.batch_id, b.code, b.name, p.package, p.unit_volume_l, SUM(p.quantity), MIN(p.at) "
        "FROM packaged_movements p JOIN batches b ON b.id = p.batch_id WHERE p.reason = 'packaged' "
        "GROUP BY p.batch_id, p.package, p.unit_volume_l ORDER BY MIN(p.at) DESC, p.batch_id DESC LIMIT ?;");
    stmt.bind(1, limit);
    std::vector<domain::PackagedStock> lines;
    while (stmt.step()) {
        domain::PackagedStock entry;
        entry.batch_id = stmt.column_int64(0);
        entry.batch_code = stmt.column_text(1);
        entry.beer = stmt.column_text(2);
        entry.type = domain::package_type_from_key(stmt.column_text(3)).value_or(domain::PackageType::HalfBarrel);
        entry.unit_volume_l = stmt.column_double(4);
        entry.count = static_cast<int>(stmt.column_int64(5));
        entry.packaged_at = column_time(stmt, 6);
        lines.push_back(std::move(entry));
    }
    return lines;
}

std::vector<PackagedMovementRow> PackagingRepository::packaged_movements(int limit) {
    auto stmt = db_.prepare(
        "SELECT p.id, p.at, p.batch_id, p.package, p.unit_volume_l, p.quantity, p.reason, p.vessel_id, p.note, "
        "u.display_name, b.code, b.name, COALESCE(e.name, '') FROM packaged_movements p "
        "JOIN users u ON u.id = p.user_id JOIN batches b ON b.id = p.batch_id "
        "LEFT JOIN elements e ON e.id = p.vessel_id ORDER BY p.at DESC, p.id DESC LIMIT ?;");
    stmt.bind(1, limit);
    std::vector<PackagedMovementRow> rows;
    while (stmt.step()) {
        PackagedMovementRow row;
        domain::PackagedMovement& m = row.movement;
        m.id = stmt.column_int64(0);
        m.at = column_time(stmt, 1);
        m.batch_id = stmt.column_int64(2);
        m.type = domain::package_type_from_key(stmt.column_text(3)).value_or(domain::PackageType::HalfBarrel);
        m.unit_volume_l = stmt.column_double(4);
        m.quantity = static_cast<int>(stmt.column_int64(5));
        m.reason = domain::packaged_reason_from_key(stmt.column_text(6)).value_or(domain::PackagedReason::Packaged);
        m.vessel_id = stmt.column_optional_int64(7);
        m.note = stmt.column_text(8);
        m.user = stmt.column_text(9);
        row.batch_code = stmt.column_text(10);
        row.beer = stmt.column_text(11);
        row.vessel_name = stmt.column_text(12);
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace ninkasi::data
