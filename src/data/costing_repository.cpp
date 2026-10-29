#include "data/costing_repository.h"

namespace ninkasi::data {

namespace {

domain::Timestamp column_time(const Statement& stmt, int col) {
    return domain::parse_iso8601(stmt.column_text(col)).value_or(domain::Timestamp{});
}

} // namespace

CostingData CostingRepository::load() {
    CostingData data;

    auto lots = db_.prepare("SELECT id, item_id, received_at, quantity, cost, supplier FROM inventory_lots;");
    while (lots.step()) {
        data.lots.push_back({lots.column_int64(0), lots.column_int64(1), column_time(lots, 2), lots.column_double(3),
                             lots.column_optional_double(4), lots.column_text(5)});
    }

    auto remaining = db_.prepare(
        "SELECT lot_id, SUM(quantity) FROM stock_movements WHERE lot_id IS NOT NULL GROUP BY lot_id "
        "HAVING SUM(quantity) > 0;");
    while (remaining.step()) data.remaining.push_back({remaining.column_int64(0), remaining.column_double(1)});

    auto movements =
        db_.prepare("SELECT id, at, item_id, lot_id, quantity, reason, batch_id FROM stock_movements;");
    while (movements.step()) {
        domain::StockMovement movement;
        movement.id = movements.column_int64(0);
        movement.at = column_time(movements, 1);
        movement.item_id = movements.column_int64(2);
        movement.lot_id = movements.column_optional_int64(3);
        movement.quantity = movements.column_double(4);
        movement.reason =
            domain::stock_reason_from_key(movements.column_text(5)).value_or(domain::StockReason::Used);
        movement.batch_id = movements.column_optional_int64(6);
        data.movements.push_back(std::move(movement));
    }

    auto packaged = db_.prepare(
        "SELECT id, at, batch_id, package, unit_volume_l, quantity, reason, sale_line_id FROM packaged_movements;");
    while (packaged.step()) {
        domain::PackagedMovement movement;
        movement.id = packaged.column_int64(0);
        movement.at = column_time(packaged, 1);
        movement.batch_id = packaged.column_int64(2);
        movement.type =
            domain::package_type_from_key(packaged.column_text(3)).value_or(domain::PackageType::HalfBarrel);
        movement.unit_volume_l = packaged.column_double(4);
        movement.quantity = static_cast<int>(packaged.column_int64(5));
        movement.reason =
            domain::packaged_reason_from_key(packaged.column_text(6)).value_or(domain::PackagedReason::Packaged);
        movement.sale_line_id = packaged.column_optional_int64(7);
        data.packaged.push_back(std::move(movement));
    }

    auto batches = db_.prepare(
        "SELECT b.id, b.code, b.name, b.status, b.finished_at, "
        "COALESCE((SELECT SUM(volume_l) FROM brews WHERE batch_id = b.id AND knocked_out_at IS NOT NULL), 0) "
        "FROM batches b;");
    while (batches.step()) {
        domain::BatchFacts batch;
        batch.id = batches.column_int64(0);
        batch.code = batches.column_text(1);
        batch.beer = batches.column_text(2);
        batch.status = domain::batch_status_from_key(batches.column_text(3)).value_or(domain::BatchStatus::Brewing);
        if (!batches.column_is_null(4)) batch.finished_at = domain::parse_iso8601(batches.column_text(4));
        batch.brewed_l = batches.column_double(5);
        data.batches.push_back(std::move(batch));
    }
    return data;
}

} // namespace ninkasi::data
