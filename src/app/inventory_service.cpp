#include "app/inventory_service.h"

#include "data/audit_log.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ninkasi::app {

namespace {

constexpr std::string_view kEntity = "inventory_item";
constexpr double kNegligible = 1e-9;

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

} // namespace

InventoryService::InventoryService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {
    refresh();
}

const ItemStock* InventoryService::find(std::int64_t item_id) const {
    for (const ItemStock& entry : stock_) {
        if (entry.item.id == item_id) return &entry;
    }
    return nullptr;
}

std::vector<domain::InventoryItem> InventoryService::items() const {
    std::vector<domain::InventoryItem> items;
    items.reserve(stock_.size());
    for (const ItemStock& entry : stock_) items.push_back(entry.item);
    return items;
}

std::vector<data::LotRow> InventoryService::lots(std::int64_t item_id) {
    return repository_.lots(item_id);
}

std::vector<data::InvoiceRow> InventoryService::invoices(int limit) {
    return repository_.invoices(limit);
}

std::vector<data::InvoiceLineRow> InventoryService::invoice_lines(std::int64_t invoice_id) {
    return repository_.invoice_lines(invoice_id);
}

std::vector<data::MovementRow> InventoryService::movements(std::optional<std::int64_t> item_id, int limit) {
    return repository_.movements(item_id, limit);
}

// --- Items ----------------------------------------------------------------------

domain::InventoryItem InventoryService::checked(domain::InventoryItem item) {
    item.name = trimmed(item.name);
    if (item.name.empty()) fail("Give the item a name.");
    if (repository_.name_taken(item.name, item.id)) fail("There is already an item called " + item.name + ".");
    if (item.reorder_level && !(*item.reorder_level >= 0.0)) fail("The reorder level can't be negative.");
    if (item.category == domain::ItemCategory::Can) {
        // Packaging takes cans by the count and needs what each holds.
        if (item.unit != domain::StockUnit::Count) fail("Cans are counted.");
        if (!item.fill_volume_l || !(*item.fill_volume_l > 0.0)) fail("Enter how much a can holds.");
    } else {
        item.fill_volume_l.reset();
    }
    return item;
}

const ItemStock& InventoryService::existing(std::int64_t item_id) const {
    const ItemStock* entry = find(item_id);
    if (!entry) fail("That item is no longer stocked.");
    return *entry;
}

std::int64_t InventoryService::add_item(const domain::InventoryItem& item) {
    domain::InventoryItem saved = checked(item);
    saved.archived = false;
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert_item(saved);
    data::record_audit(db_, session_.user_id, "inventory.create", kEntity, id, "\"" + saved.name + "\"");
    tx.commit();
    refresh();
    return id;
}

void InventoryService::update_item(const domain::InventoryItem& item) {
    existing(item.id);
    const domain::InventoryItem saved = checked(item);
    data::Transaction tx(db_);
    repository_.update_item(saved);
    data::record_audit(db_, session_.user_id, "inventory.update", kEntity, saved.id, "\"" + saved.name + "\"");
    tx.commit();
    refresh();
}

void InventoryService::remove_item(std::int64_t item_id) {
    domain::InventoryItem item = existing(item_id).item;
    data::Transaction tx(db_);
    if (repository_.has_history(item_id)) {
        // Its lots and movements are history: keep them, stop listing the item.
        item.archived = true;
        repository_.update_item(item);
        data::record_audit(db_, session_.user_id, "inventory.archive", kEntity, item_id, "\"" + item.name + "\"");
    } else {
        repository_.delete_item(item_id);
        data::record_audit(db_, session_.user_id, "inventory.delete", kEntity, item_id, "\"" + item.name + "\"");
    }
    tx.commit();
    refresh();
}

// --- Stock ----------------------------------------------------------------------

std::int64_t InventoryService::receive(const domain::Invoice& invoice) {
    if (invoice.lines.empty()) fail("Add the items received.");
    for (const domain::InvoiceLine& line : invoice.lines) {
        const ItemStock& entry = existing(line.item_id);
        if (!(line.quantity > 0.0)) fail("Enter how much " + entry.item.name + " was received.");
        if (line.cost && !(*line.cost >= 0.0)) fail("The cost of " + entry.item.name + " can't be negative.");
    }

    domain::Invoice saved = invoice;
    saved.supplier = trimmed(invoice.supplier);
    saved.number = trimmed(invoice.number);
    saved.note = trimmed(invoice.note);

    data::Transaction tx(db_);
    const std::int64_t invoice_id = repository_.insert_invoice(saved, session_.user_id);
    for (const domain::InvoiceLine& line : saved.lines) {
        domain::Lot lot;
        lot.item_id = line.item_id;
        lot.invoice_id = invoice_id;
        lot.received_at = saved.received_at;
        lot.quantity = line.quantity;
        lot.supplier = saved.supplier;
        lot.lot_code = trimmed(line.lot_code);
        lot.cost = line.cost;
        lot.best_before = line.best_before;
        add_lot(lot);
    }
    tx.commit();
    refresh();
    return invoice_id;
}

void InventoryService::add_lot(const domain::Lot& lot) {
    const std::int64_t lot_id = repository_.insert_lot(lot);
    repository_.insert_movement({.at = lot.received_at, .item_id = lot.item_id, .lot_id = lot_id,
                                 .quantity = lot.quantity, .reason = domain::StockReason::Received},
                                std::nullopt, session_.user_id);

    // Stock used before this delivery was logged came out of it: move the
    // shortfall onto the lot.
    const double shortfall = -repository_.unlotted(lot.item_id);
    if (shortfall > kNegligible) {
        const double settled = std::min(shortfall, lot.quantity);
        repository_.insert_movement({.at = lot.received_at, .item_id = lot.item_id, .lot_id = lot_id,
                                     .quantity = -settled, .reason = domain::StockReason::Settled},
                                    std::nullopt, session_.user_id);
        repository_.insert_movement({.at = lot.received_at, .item_id = lot.item_id, .lot_id = std::nullopt,
                                     .quantity = settled, .reason = domain::StockReason::Settled},
                                    std::nullopt, session_.user_id);
    }
}

void InventoryService::count(std::int64_t item_id, double counted, domain::Timestamp at, std::string_view note) {
    existing(item_id);
    if (!(counted >= 0.0)) fail("A count can't be negative.");

    data::Transaction tx(db_);
    const double change = counted - repository_.balance(item_id);
    if (change > kNegligible) {
        // Found more than recorded: it isn't from any known delivery.
        repository_.insert_movement({.at = at, .item_id = item_id, .lot_id = std::nullopt, .quantity = change,
                                     .reason = domain::StockReason::Counted, .note = std::string(note)},
                                    std::nullopt, session_.user_id);
    } else if (change < -kNegligible) {
        draw(item_id, -change, domain::StockReason::Counted, at, std::nullopt, std::nullopt, note);
    }
    tx.commit();
    refresh();
}

void InventoryService::take(std::int64_t item_id, double quantity, domain::StockReason reason, domain::Timestamp at,
                            std::string_view note) {
    existing(item_id);
    if (reason != domain::StockReason::Used && reason != domain::StockReason::Discarded) {
        fail("Stock can only be taken out as used or discarded.");
    }
    if (!(quantity > 0.0)) fail("Enter the quantity taken.");
    data::Transaction tx(db_);
    draw(item_id, quantity, reason, at, std::nullopt, std::nullopt, note);
    tx.commit();
    refresh();
}

void InventoryService::write_brew_usage(std::int64_t batch_id,
                                        std::span<const std::pair<std::int64_t, domain::IngredientUse>> uses,
                                        domain::Timestamp at) {
    const std::vector<domain::InventoryItem> stocked = repository_.load_items(false);
    for (const auto& [brew_ingredient_id, use] : uses) {
        const domain::InventoryItem* item = domain::find_stock_for(use, stocked);
        if (!item || !(use.actual > 0.0)) continue;
        draw(item->id, use.actual, domain::StockReason::Brewed, at, batch_id, brew_ingredient_id, {});
    }
}

void InventoryService::draw(std::int64_t item_id, double quantity, domain::StockReason reason, domain::Timestamp at,
                            std::optional<std::int64_t> batch_id, std::optional<std::int64_t> brew_ingredient_id,
                            std::string_view note) {
    std::vector<domain::LotBalance> balances;
    for (const data::LotRow& row : repository_.lots(item_id)) balances.push_back({row.lot.id, row.remaining});
    for (const domain::Draw& part : domain::plan_draw(balances, quantity)) {
        repository_.insert_movement({.at = at, .item_id = item_id, .lot_id = part.lot_id, .quantity = -part.quantity,
                                     .reason = reason, .batch_id = batch_id, .note = std::string(note)},
                                    brew_ingredient_id, session_.user_id);
    }
}

void InventoryService::refresh() {
    const std::unordered_map<std::int64_t, double> balances = repository_.balances();
    stock_.clear();
    for (domain::InventoryItem& item : repository_.load_items(false)) {
        const auto found = balances.find(item.id);
        stock_.push_back({std::move(item), found == balances.end() ? 0.0 : found->second});
    }
    ++revision_;
}

} // namespace ninkasi::app
