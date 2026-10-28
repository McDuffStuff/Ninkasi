#include "data/inventory_repository.h"

namespace ninkasi::data {

namespace {

constexpr const char* kItemColumns =
    "id, name, category, unit, library_id, reorder_level, notes, archived, fill_volume_l";

std::optional<domain::InventoryItem> read_item(const Statement& stmt) {
    const std::optional<domain::ItemCategory> category = domain::item_category_from_key(stmt.column_text(2));
    const std::optional<domain::StockUnit> unit = domain::stock_unit_from_key(stmt.column_text(3));
    if (!category || !unit) return std::nullopt;
    domain::InventoryItem item;
    item.id = stmt.column_int64(0);
    item.name = stmt.column_text(1);
    item.category = *category;
    item.unit = *unit;
    item.library_id = stmt.column_optional_int64(4).value_or(0);
    item.reorder_level = stmt.column_optional_double(5);
    item.notes = stmt.column_text(6);
    item.archived = stmt.column_int64(7) != 0;
    item.fill_volume_l = stmt.column_optional_double(8);
    return item;
}

void bind_item(Statement& stmt, const domain::InventoryItem& item) {
    stmt.bind(1, item.name).bind(2, domain::key(item.category)).bind(3, domain::key(item.unit));
    stmt.bind_optional(4, item.library_id != 0 ? std::optional<std::int64_t>(item.library_id) : std::nullopt);
    stmt.bind_optional(5, item.reorder_level);
    stmt.bind(6, item.notes).bind(7, item.archived ? 1 : 0);
    stmt.bind_optional(8, item.fill_volume_l);
}

domain::Timestamp column_time(const Statement& stmt, int col) {
    return domain::parse_iso8601(stmt.column_text(col)).value_or(domain::Timestamp{});
}

} // namespace

// --- Items --------------------------------------------------------------------

std::vector<domain::InventoryItem> InventoryRepository::load_items(bool include_archived) {
    std::vector<domain::InventoryItem> items;
    auto stmt = db_.prepare(std::string("SELECT ") + kItemColumns + " FROM inventory_items" +
                            (include_archived ? "" : " WHERE archived = 0") + " ORDER BY name COLLATE NOCASE, id;");
    while (stmt.step()) {
        if (std::optional<domain::InventoryItem> item = read_item(stmt)) items.push_back(std::move(*item));
    }
    return items;
}

std::int64_t InventoryRepository::insert_item(const domain::InventoryItem& item) {
    auto stmt = db_.prepare(
        "INSERT INTO inventory_items (name, category, unit, library_id, reorder_level, notes, archived, "
        "fill_volume_l) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    bind_item(stmt, item);
    stmt.execute();
    return db_.last_insert_id();
}

void InventoryRepository::update_item(const domain::InventoryItem& item) {
    auto stmt = db_.prepare(
        "UPDATE inventory_items SET name = ?, category = ?, unit = ?, library_id = ?, reorder_level = ?, "
        "notes = ?, archived = ?, fill_volume_l = ? WHERE id = ?;");
    bind_item(stmt, item);
    stmt.bind(9, item.id);
    stmt.execute();
}

void InventoryRepository::delete_item(std::int64_t item_id) {
    auto stmt = db_.prepare("DELETE FROM inventory_items WHERE id = ?;");
    stmt.bind(1, item_id);
    stmt.execute();
}

bool InventoryRepository::has_history(std::int64_t item_id) {
    auto stmt = db_.prepare(
        "SELECT 1 FROM stock_movements WHERE item_id = ?1 UNION ALL SELECT 1 FROM inventory_lots WHERE item_id = ?1 "
        "LIMIT 1;");
    stmt.bind(1, item_id);
    return stmt.step();
}

bool InventoryRepository::name_taken(const std::string& name, std::int64_t except_id) {
    auto stmt = db_.prepare(
        "SELECT 1 FROM inventory_items WHERE name = ? COLLATE NOCASE AND id <> ? AND archived = 0 LIMIT 1;");
    stmt.bind(1, name).bind(2, except_id);
    return stmt.step();
}

// --- Invoices and lots ---------------------------------------------------------

std::int64_t InventoryRepository::insert_invoice(const domain::Invoice& invoice, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO invoices (received_at, supplier, number, note, user_id) VALUES (?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(invoice.received_at)).bind(2, invoice.supplier).bind(3, invoice.number);
    stmt.bind(4, invoice.note).bind(5, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

std::vector<InvoiceRow> InventoryRepository::invoices(int limit) {
    std::vector<InvoiceRow> rows;
    auto stmt = db_.prepare(
        "SELECT v.id, v.received_at, v.supplier, v.number, v.note, u.display_name, "
        "(SELECT COUNT(*) FROM inventory_lots WHERE invoice_id = v.id), "
        "(SELECT SUM(cost) FROM inventory_lots WHERE invoice_id = v.id) "
        "FROM invoices v JOIN users u ON u.id = v.user_id ORDER BY v.received_at DESC, v.id DESC LIMIT ?;");
    stmt.bind(1, limit);
    while (stmt.step()) {
        InvoiceRow row;
        row.id = stmt.column_int64(0);
        row.received_at = column_time(stmt, 1);
        row.supplier = stmt.column_text(2);
        row.number = stmt.column_text(3);
        row.note = stmt.column_text(4);
        row.user = stmt.column_text(5);
        row.lines = static_cast<int>(stmt.column_int64(6));
        row.total = stmt.column_optional_double(7);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<InvoiceLineRow> InventoryRepository::invoice_lines(std::int64_t invoice_id) {
    std::vector<InvoiceLineRow> rows;
    auto stmt = db_.prepare(
        "SELECT l.id, l.item_id, l.received_at, l.quantity, l.supplier, l.lot_code, l.cost, l.best_before, l.note, "
        "i.name, i.category, i.unit FROM inventory_lots l JOIN inventory_items i ON i.id = l.item_id "
        "WHERE l.invoice_id = ? ORDER BY l.id;");
    stmt.bind(1, invoice_id);
    while (stmt.step()) {
        InvoiceLineRow row;
        row.lot.id = stmt.column_int64(0);
        row.lot.item_id = stmt.column_int64(1);
        row.lot.invoice_id = invoice_id;
        row.lot.received_at = column_time(stmt, 2);
        row.lot.quantity = stmt.column_double(3);
        row.lot.supplier = stmt.column_text(4);
        row.lot.lot_code = stmt.column_text(5);
        row.lot.cost = stmt.column_optional_double(6);
        if (!stmt.column_is_null(7)) row.lot.best_before = domain::parse_date(stmt.column_text(7));
        row.lot.note = stmt.column_text(8);
        row.item_name = stmt.column_text(9);
        row.category = domain::item_category_from_key(stmt.column_text(10)).value_or(domain::ItemCategory::Other);
        row.unit = domain::stock_unit_from_key(stmt.column_text(11)).value_or(domain::StockUnit::Mass);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::int64_t InventoryRepository::insert_lot(const domain::Lot& lot) {
    auto stmt = db_.prepare(
        "INSERT INTO inventory_lots (item_id, received_at, quantity, supplier, lot_code, cost, best_before, note, "
        "invoice_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, lot.item_id).bind(2, domain::to_iso8601(lot.received_at)).bind(3, lot.quantity);
    stmt.bind(4, lot.supplier).bind(5, lot.lot_code);
    stmt.bind_optional(6, lot.cost);
    if (lot.best_before) {
        stmt.bind(7, domain::to_iso8601(*lot.best_before));
    } else {
        stmt.bind_null(7);
    }
    stmt.bind(8, lot.note);
    stmt.bind_optional(9, lot.invoice_id);
    stmt.execute();
    return db_.last_insert_id();
}

std::vector<LotRow> InventoryRepository::lots(std::int64_t item_id) {
    std::vector<LotRow> rows;
    auto stmt = db_.prepare(
        "SELECT l.id, l.item_id, l.received_at, l.quantity, l.supplier, l.lot_code, l.cost, l.best_before, l.note, "
        "COALESCE((SELECT SUM(quantity) FROM stock_movements WHERE lot_id = l.id), 0), l.invoice_id "
        "FROM inventory_lots l WHERE l.item_id = ? ORDER BY l.received_at, l.id;");
    stmt.bind(1, item_id);
    while (stmt.step()) {
        LotRow row;
        row.lot.id = stmt.column_int64(0);
        row.lot.item_id = stmt.column_int64(1);
        row.lot.received_at = column_time(stmt, 2);
        row.lot.quantity = stmt.column_double(3);
        row.lot.supplier = stmt.column_text(4);
        row.lot.lot_code = stmt.column_text(5);
        row.lot.cost = stmt.column_optional_double(6);
        if (!stmt.column_is_null(7)) row.lot.best_before = domain::parse_date(stmt.column_text(7));
        row.lot.note = stmt.column_text(8);
        row.remaining = stmt.column_double(9);
        row.lot.invoice_id = stmt.column_optional_int64(10);
        rows.push_back(std::move(row));
    }
    return rows;
}

// --- Ledger -------------------------------------------------------------------

void InventoryRepository::insert_movement(const domain::StockMovement& movement,
                                          std::optional<std::int64_t> brew_ingredient_id, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO stock_movements (at, item_id, lot_id, quantity, reason, batch_id, brew_ingredient_id, note, "
        "user_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
    stmt.bind(1, domain::to_iso8601(movement.at)).bind(2, movement.item_id);
    stmt.bind_optional(3, movement.lot_id);
    stmt.bind(4, movement.quantity).bind(5, domain::key(movement.reason));
    stmt.bind_optional(6, movement.batch_id);
    stmt.bind_optional(7, brew_ingredient_id);
    stmt.bind(8, movement.note).bind(9, user_id);
    stmt.execute();
}

std::unordered_map<std::int64_t, double> InventoryRepository::balances() {
    std::unordered_map<std::int64_t, double> balances;
    auto stmt = db_.prepare("SELECT item_id, SUM(quantity) FROM stock_movements GROUP BY item_id;");
    while (stmt.step()) balances[stmt.column_int64(0)] = stmt.column_double(1);
    return balances;
}

double InventoryRepository::balance(std::int64_t item_id) {
    auto stmt = db_.prepare("SELECT COALESCE(SUM(quantity), 0) FROM stock_movements WHERE item_id = ?;");
    stmt.bind(1, item_id);
    stmt.step();
    return stmt.column_double(0);
}

double InventoryRepository::unlotted(std::int64_t item_id) {
    auto stmt = db_.prepare(
        "SELECT COALESCE(SUM(quantity), 0) FROM stock_movements WHERE item_id = ? AND lot_id IS NULL;");
    stmt.bind(1, item_id);
    stmt.step();
    return stmt.column_double(0);
}

std::vector<MovementRow> InventoryRepository::movements(std::optional<std::int64_t> item_id, int limit) {
    const std::string where = item_id ? "m.item_id = ?2" : "1 = 1";
    auto stmt = db_.prepare(
        "SELECT m.id, m.at, m.item_id, m.lot_id, m.quantity, m.reason, m.batch_id, m.note, u.display_name, "
        "i.name, i.category, i.unit, COALESCE(l.lot_code, ''), COALESCE(b.code, ''), COALESCE(l.supplier, ''), "
        "COALESCE(v.number, '') "
        "FROM stock_movements m JOIN inventory_items i ON i.id = m.item_id JOIN users u ON u.id = m.user_id "
        "LEFT JOIN inventory_lots l ON l.id = m.lot_id LEFT JOIN batches b ON b.id = m.batch_id "
        "LEFT JOIN invoices v ON v.id = l.invoice_id "
        "WHERE " + where + " ORDER BY m.at DESC, m.id DESC LIMIT ?1;");
    stmt.bind(1, limit);
    if (item_id) stmt.bind(2, *item_id);
    std::vector<MovementRow> rows;
    while (stmt.step()) {
        MovementRow row;
        domain::StockMovement& m = row.movement;
        m.id = stmt.column_int64(0);
        m.at = column_time(stmt, 1);
        m.item_id = stmt.column_int64(2);
        m.lot_id = stmt.column_optional_int64(3);
        m.quantity = stmt.column_double(4);
        m.reason = domain::stock_reason_from_key(stmt.column_text(5)).value_or(domain::StockReason::Counted);
        m.batch_id = stmt.column_optional_int64(6);
        m.note = stmt.column_text(7);
        m.user = stmt.column_text(8);
        row.item_name = stmt.column_text(9);
        row.category = domain::item_category_from_key(stmt.column_text(10)).value_or(domain::ItemCategory::Other);
        row.unit = domain::stock_unit_from_key(stmt.column_text(11)).value_or(domain::StockUnit::Mass);
        row.lot_code = stmt.column_text(12);
        row.batch_code = stmt.column_text(13);
        row.supplier = stmt.column_text(14);
        row.invoice_number = stmt.column_text(15);
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace ninkasi::data
