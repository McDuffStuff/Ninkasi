#pragma once
// Reads and writes inventory items, lots and the stock ledger.
// InventoryService decides what may be written; this only stores it.

#include "data/database.h"
#include "domain/inventory.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ninkasi::data {

// A lot and what's left of it.
struct LotRow {
    domain::Lot lot;
    double remaining = 0.0;
};

// A ledger row with what a log shows beside it.
struct MovementRow {
    domain::StockMovement movement;
    std::string item_name;
    domain::ItemCategory category = domain::ItemCategory::Other;
    domain::StockUnit unit = domain::StockUnit::Mass;
    std::string lot_code;
    std::string batch_code;
};

class InventoryRepository {
public:
    explicit InventoryRepository(Database& db) : db_(db) {}

    // --- Items --------------------------------------------------------------
    std::vector<domain::InventoryItem> load_items(bool include_archived);
    // Inserts the item (its id is ignored) and returns the new id.
    std::int64_t insert_item(const domain::InventoryItem& item);
    void update_item(const domain::InventoryItem& item);
    void delete_item(std::int64_t item_id);
    bool has_history(std::int64_t item_id);
    bool name_taken(const std::string& name, std::int64_t except_id);

    // --- Lots ---------------------------------------------------------------
    std::int64_t insert_lot(const domain::Lot& lot);
    // An item's lots, oldest first, with what's left of each.
    std::vector<LotRow> lots(std::int64_t item_id);

    // --- Ledger -------------------------------------------------------------
    void insert_movement(const domain::StockMovement& movement, std::optional<std::int64_t> brew_ingredient_id,
                         std::int64_t user_id);
    // What's on hand, by item.
    std::unordered_map<std::int64_t, double> balances();
    double balance(std::int64_t item_id);
    // The item's stock not tied to a lot: negative when stock was used
    // before its delivery was logged.
    double unlotted(std::int64_t item_id);
    // Movements, newest first: of one item, or of the items kept in one
    // storage element, or of everything.
    std::vector<MovementRow> movements(std::optional<std::int64_t> item_id, std::optional<std::int64_t> location_id,
                                       int limit);

private:
    Database& db_;
};

} // namespace ninkasi::data
