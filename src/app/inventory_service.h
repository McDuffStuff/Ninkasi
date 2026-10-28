#pragma once
// Inventory: what the brewery stocks, deliveries received, stock counts,
// and stock used or discarded. Brews draw on it at knock-out (see
// BatchService), from the amounts the brewer confirmed.
//
// Every change is a row in the stock ledger (stock_movements); stock is
// drawn first in, first out (domain::plan_draw). Nothing is ever refused for
// lack of stock: records are entered after the fact, so stock can be used
// before its delivery is logged. The shortfall shows as negative stock until
// that delivery arrives and settles it.
//
// Changes run in one transaction each; items being added, changed or removed
// are audited. What's on hand is kept in memory for the floor and lists.

#include "app/session.h"
#include "data/database.h"
#include "data/inventory_repository.h"
#include "domain/batch.h"
#include "domain/inventory.h"
#include "domain/timestamp.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ninkasi::app {

// An item and what's on hand.
struct ItemStock {
    domain::InventoryItem item;
    double on_hand = 0.0;

    bool low() const { return item.reorder_level && on_hand < *item.reorder_level; }
};

// A delivery.
struct Receipt {
    std::int64_t item_id = 0;
    domain::Timestamp at{};
    double quantity = 0.0;
    std::string supplier;
    std::string lot_code;
    std::optional<double> cost;
    std::optional<domain::Date> best_before;
    std::string note;
};

class InventoryService {
public:
    InventoryService(data::Database& db, Session session);

    // --- In memory ------------------------------------------------------------

    // Items still stocked, by name, with what's on hand.
    const std::vector<ItemStock>& stock() const { return stock_; }
    const ItemStock* find(std::int64_t item_id) const;
    // The items still stocked, for matching brew ingredients to them.
    std::vector<domain::InventoryItem> items() const;
    // Grows with every change, so windows know when to re-read the database.
    std::uint64_t revision() const { return revision_; }

    // --- From the database ----------------------------------------------------

    std::vector<data::LotRow> lots(std::int64_t item_id);
    // Newest first: of one item, or of everything.
    std::vector<data::MovementRow> movements(std::optional<std::int64_t> item_id, int limit = 500);

    // --- Items ----------------------------------------------------------------

    // Each throws std::invalid_argument if the name is blank or another item
    // already has it. add_item returns the new id.
    std::int64_t add_item(const domain::InventoryItem& item);
    void update_item(const domain::InventoryItem& item);
    // Deletes an item that has never been stocked; archives one with history.
    void remove_item(std::int64_t item_id);

    // --- Stock ----------------------------------------------------------------

    // Records a delivery as a new lot. Stock used before it was logged is
    // settled from it. Throws if the quantity isn't positive. Returns the lot's id.
    std::int64_t receive(const Receipt& receipt);
    // Replaces the recorded stock with what was counted.
    void count(std::int64_t item_id, double counted, domain::Timestamp at, std::string_view note);
    // Takes stock out for something other than a brew: used or discarded.
    void take(std::int64_t item_id, double quantity, domain::StockReason reason, domain::Timestamp at,
              std::string_view note);

    // For a knock-out: draws each confirmed ingredient (with its
    // brew_ingredients row id) from the item it's stocked as. Ingredients
    // that aren't stocked are skipped. Call inside the caller's transaction,
    // and refresh() once it's committed.
    void write_brew_usage(std::int64_t batch_id,
                          std::span<const std::pair<std::int64_t, domain::IngredientUse>> uses,
                          domain::Timestamp at);

    // Re-reads what's on hand.
    void refresh();

private:
    // Writes the ledger rows taking `quantity` of an item, first in, first out.
    void draw(std::int64_t item_id, double quantity, domain::StockReason reason, domain::Timestamp at,
              std::optional<std::int64_t> batch_id, std::optional<std::int64_t> brew_ingredient_id,
              std::string_view note);
    domain::InventoryItem checked(domain::InventoryItem item);
    const ItemStock& existing(std::int64_t item_id) const;

    data::Database& db_;
    Session session_;
    data::InventoryRepository repository_;
    std::vector<ItemStock> stock_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
