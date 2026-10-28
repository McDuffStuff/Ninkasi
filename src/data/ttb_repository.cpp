#include "data/ttb_repository.h"

namespace ninkasi::data {

namespace {

domain::Timestamp column_time(const Statement& stmt, int col) {
    return domain::parse_iso8601(stmt.column_text(col)).value_or(domain::Timestamp{});
}

} // namespace

// Times are stored as "YYYY-MM-DDTHH:MM:SSZ", which sort as text in time order.

std::vector<domain::VolumeEvent> TtbRepository::volume_events(domain::Timestamp end) {
    std::vector<domain::VolumeEvent> events;
    auto stmt = db_.prepare(
        "SELECT id, at, batch_id, vessel_id, volume_l, reason FROM volume_events WHERE at < ? ORDER BY at, id;");
    stmt.bind(1, domain::to_iso8601(end));
    while (stmt.step()) {
        events.push_back({stmt.column_int64(0), column_time(stmt, 1), stmt.column_int64(2),
                          stmt.column_optional_int64(3), stmt.column_double(4),
                          domain::volume_reason_from_key(stmt.column_text(5)).value_or(domain::VolumeReason::Adjusted)});
    }
    return events;
}

std::vector<domain::PackagedMovement> TtbRepository::packaged_movements(domain::Timestamp end) {
    std::vector<domain::PackagedMovement> movements;
    auto stmt = db_.prepare(
        "SELECT id, at, batch_id, package, unit_volume_l, quantity, reason, vessel_id FROM packaged_movements "
        "WHERE at < ? ORDER BY at, id;");
    stmt.bind(1, domain::to_iso8601(end));
    while (stmt.step()) {
        domain::PackagedMovement m;
        m.id = stmt.column_int64(0);
        m.at = column_time(stmt, 1);
        m.batch_id = stmt.column_int64(2);
        m.type = domain::package_type_from_key(stmt.column_text(3)).value_or(domain::PackageType::HalfBarrel);
        m.unit_volume_l = stmt.column_double(4);
        m.quantity = static_cast<int>(stmt.column_int64(5));
        m.reason = domain::packaged_reason_from_key(stmt.column_text(6)).value_or(domain::PackagedReason::Packaged);
        m.vessel_id = stmt.column_optional_int64(7);
        movements.push_back(std::move(m));
    }
    return movements;
}

} // namespace ninkasi::data
