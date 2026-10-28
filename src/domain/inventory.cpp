#include "domain/inventory.h"

#include <algorithm>
#include <cctype>

namespace ninkasi::domain {

namespace {

// Smaller than any real quantity: what's left after rounding.
constexpr double kNegligible = 1e-9;

template <typename E>
struct EnumText {
    E value;
    std::string_view key;   // stored in the database
    std::string_view name;  // shown to the brewer
};

constexpr EnumText<ItemCategory> kCategoryTexts[] = {
    {ItemCategory::Grain,     "grain",     "Grain"},
    {ItemCategory::Hop,       "hop",       "Hops"},
    {ItemCategory::Yeast,     "yeast",     "Yeast"},
    {ItemCategory::Adjunct,   "adjunct",   "Sugars & adjuncts"},
    {ItemCategory::Chemical,  "chemical",  "Salts & chemicals"},
    {ItemCategory::Packaging, "packaging", "Packaging"},
    {ItemCategory::Other,     "other",     "Other"},
};

constexpr EnumText<StockUnit> kUnitTexts[] = {
    {StockUnit::Mass,   "mass",   "By weight"},
    {StockUnit::Volume, "volume", "By volume"},
    {StockUnit::Count,  "count",  "By count"},
};

constexpr EnumText<StockReason> kReasonTexts[] = {
    {StockReason::Received,  "received",  "Received"},
    {StockReason::Brewed,    "brewed",    "Brewed"},
    {StockReason::Counted,   "counted",   "Stock count"},
    {StockReason::Used,      "used",      "Used"},
    {StockReason::Discarded, "discarded", "Discarded"},
    {StockReason::Settled,   "settled",   "Settled shortfall"},
};

template <typename E, std::size_t N>
const EnumText<E>& lookup(E value, const EnumText<E> (&table)[N]) {
    for (const EnumText<E>& entry : table) {
        if (entry.value == value) return entry;
    }
    return table[0];
}

template <typename E, std::size_t N>
std::optional<E> from_key(std::string_view key, const EnumText<E> (&table)[N]) {
    for (const EnumText<E>& entry : table) {
        if (entry.key == key) return entry.value;
    }
    return std::nullopt;
}

bool equal_ignoring_case(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

std::vector<Draw> plan_draw(std::span<const LotBalance> lots, double quantity) {
    std::vector<Draw> draws;
    double left = quantity;
    for (const LotBalance& lot : lots) {
        if (left <= kNegligible) break;
        if (lot.remaining <= kNegligible) continue;
        const double take = std::min(lot.remaining, left);
        draws.push_back({lot.lot_id, take});
        left -= take;
    }
    // The rest comes from unlotted stock, which goes negative if there isn't any.
    if (left > kNegligible) draws.push_back({std::nullopt, left});
    return draws;
}

std::optional<StockUnit> stock_unit_of(AmountKind kind) {
    switch (kind) {
    case AmountKind::Mass:   return StockUnit::Mass;
    case AmountKind::Volume: return StockUnit::Volume;
    case AmountKind::Count:  return StockUnit::Count;
    case AmountKind::Text:   return std::nullopt;
    }
    return std::nullopt;
}

const InventoryItem* find_stock_for(const IngredientUse& use, std::span<const InventoryItem> items) {
    const std::optional<StockUnit> unit = stock_unit_of(use.amount_kind);
    if (!unit) return nullptr;
    const InventoryItem* by_name = nullptr;
    for (const InventoryItem& item : items) {
        if (item.archived || item.unit != *unit) continue;
        if (use.library_id != 0 && item.library_id == use.library_id) return &item;
        if (!by_name && equal_ignoring_case(item.name, use.name)) by_name = &item;
    }
    return by_name;
}

std::optional<IngredientKind> ingredient_kind_of(ItemCategory category) {
    switch (category) {
    case ItemCategory::Grain:
    case ItemCategory::Adjunct: return IngredientKind::Fermentable;
    case ItemCategory::Hop:     return IngredientKind::Hop;
    case ItemCategory::Yeast:   return IngredientKind::Yeast;
    default:                    return std::nullopt;
    }
}

std::string_view name(ItemCategory category) { return lookup(category, kCategoryTexts).name; }
std::string_view name(StockUnit unit) { return lookup(unit, kUnitTexts).name; }
std::string_view name(StockReason reason) { return lookup(reason, kReasonTexts).name; }
std::string_view key(ItemCategory category) { return lookup(category, kCategoryTexts).key; }
std::string_view key(StockUnit unit) { return lookup(unit, kUnitTexts).key; }
std::string_view key(StockReason reason) { return lookup(reason, kReasonTexts).key; }
std::optional<ItemCategory> item_category_from_key(std::string_view text) { return from_key(text, kCategoryTexts); }
std::optional<StockUnit> stock_unit_from_key(std::string_view text) { return from_key(text, kUnitTexts); }
std::optional<StockReason> stock_reason_from_key(std::string_view text) { return from_key(text, kReasonTexts); }

} // namespace ninkasi::domain
