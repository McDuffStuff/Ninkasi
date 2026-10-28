#pragma once
// Inventory: the things a brewery stocks, the invoices and lots they arrive
// in, and every movement of stock in and out.
//
// Stock is drawn first in, first out: from the oldest lot that has any left.
// Records are often entered after the fact, so stock can be used before its
// delivery is logged. That shortfall is kept as a negative balance not tied
// to any lot ("unlotted"), and the next delivery settles it.

#include "domain/batch.h"
#include "domain/timestamp.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

enum class ItemCategory {
    Grain,
    Hop,
    Yeast,
    Adjunct,
    Chemical,
    Can,        // empty cans, filled when beer is packaged
    Packaging,  // lids, labels, carriers...
    Other,
};

// How an item is measured.
enum class StockUnit {
    Mass,    // kilograms
    Volume,  // liters
    Count,   // each: packs, cases, labels...
};

inline constexpr std::array kItemCategories = {ItemCategory::Grain,    ItemCategory::Hop,      ItemCategory::Yeast,
                                               ItemCategory::Adjunct,  ItemCategory::Chemical, ItemCategory::Can,
                                               ItemCategory::Packaging, ItemCategory::Other};
inline constexpr std::array kStockUnits = {StockUnit::Mass, StockUnit::Volume, StockUnit::Count};

struct InventoryItem {
    std::int64_t id = 0;
    std::string name;
    ItemCategory category = ItemCategory::Grain;
    StockUnit unit = StockUnit::Mass;
    std::optional<double> fill_volume_l;  // cans: what one holds (cans are counted)
    std::int64_t library_id = 0;          // the ingredient library entry it stocks; 0 if none
    std::optional<double> reorder_level;  // low stock below this
    std::string notes;
    bool archived = false;                // no longer stocked; kept for its history

    bool operator==(const InventoryItem&) const = default;
};

// One delivery of an item: a line of an invoice.
struct Lot {
    std::int64_t id = 0;
    std::int64_t item_id = 0;
    std::optional<std::int64_t> invoice_id;  // the invoice it arrived on
    Timestamp received_at{};
    double quantity = 0.0;            // as received
    std::string supplier;
    std::string lot_code;             // the supplier's lot number
    std::optional<double> cost;       // what the delivery cost, in the brewery's currency
    std::optional<Date> best_before;
    std::string note;
};

// One item on an invoice, which arrives as a lot.
struct InvoiceLine {
    std::int64_t item_id = 0;
    double quantity = 0.0;
    std::optional<double> cost;  // the line's total, in the brewery's currency
    std::string lot_code;        // the supplier's lot number
    std::optional<Date> best_before;
};

// A delivery as the supplier invoiced it: everything on it arrives together.
struct Invoice {
    std::int64_t id = 0;
    Timestamp received_at{};
    std::string supplier;
    std::string number;  // the supplier's invoice number
    std::string note;
    std::vector<InvoiceLine> lines;
};

// Why stock changed. Stored keys: never change one once shipped.
enum class StockReason {
    Received,
    Brewed,     // used in a brew, at knock-out
    Packaged,   // filled with beer (cans), at packaging
    Counted,    // a stock count replaced the recorded quantity
    Used,       // used for something other than a brew
    Discarded,  // spoiled, expired, spilled...
    Settled,    // a delivery covering stock used before it was logged
};

// One row of the stock ledger.
struct StockMovement {
    std::int64_t id = 0;
    Timestamp at{};
    std::int64_t item_id = 0;
    std::optional<std::int64_t> lot_id;  // nullopt: the item's unlotted balance
    double quantity = 0.0;               // positive in, negative out
    StockReason reason = StockReason::Received;
    std::optional<std::int64_t> batch_id;
    std::string note;
    std::string user;                    // who recorded it, when read back
};

// --- Drawing stock first in, first out -------------------------------------------

// What's left of a lot, in the order lots are drawn from (oldest first).
struct LotBalance {
    std::int64_t lot_id = 0;
    double remaining = 0.0;
};

// Part of a draw: from a lot, or from the unlotted balance (nullopt).
struct Draw {
    std::optional<std::int64_t> lot_id;
    double quantity = 0.0;  // positive: the amount taken

    bool operator==(const Draw&) const = default;
};

// How to take `quantity` from stock: from each lot with some left, oldest
// first, then the rest from the unlotted balance (which goes negative if
// there's not enough). Quantities of zero or less draw nothing.
std::vector<Draw> plan_draw(std::span<const LotBalance> lots, double quantity);

// --- Linking brews to stock -------------------------------------------------------

// The stock unit an ingredient's amounts are in, or nullopt for free-text
// amounts that can't be deducted.
std::optional<StockUnit> stock_unit_of(AmountKind kind);

// The item a brew ingredient is drawn from: one stocking the same library
// entry, else one with the same name (ignoring case); measured the same way
// and not archived. nullptr if none.
const InventoryItem* find_stock_for(const IngredientUse& use, std::span<const InventoryItem> items);

// The library ingredient kind an item category stocks, if any.
std::optional<IngredientKind> ingredient_kind_of(ItemCategory category);

// --- Names and stored keys --------------------------------------------------------

std::string_view name(ItemCategory category);
std::string_view name(StockUnit unit);
std::string_view name(StockReason reason);
std::string_view key(ItemCategory category);
std::string_view key(StockUnit unit);
std::string_view key(StockReason reason);
std::optional<ItemCategory> item_category_from_key(std::string_view text);
std::optional<StockUnit> stock_unit_from_key(std::string_view text);
std::optional<StockReason> stock_reason_from_key(std::string_view text);

} // namespace ninkasi::domain
