#include "domain/element.h"

#include <iterator>

namespace ninkasi::domain {

namespace {

// One row per ElementType, in enum order (checked below).
constexpr ElementTypeInfo kTypes[] = {
    // Vessels are drawn as circles, so their footprints are square.
    {ElementType::Brewhouse,     "brewhouse",      "Brewhouse",      160.0, 160.0, true},
    {ElementType::Fermenter,     "fermenter",      "Fermenter",      130.0, 130.0, true},
    {ElementType::BriteTank,     "brite_tank",     "Brite tank",     120.0, 120.0, true},
    // Inventory's and the keg cleaner's keys are from when they were "dry
    // storage" and "keg washer".
    {ElementType::Inventory,     "dry_storage",    "Inventory",      200.0, 140.0, false},
    {ElementType::PackagedGoods, "packaged_goods", "Packaged goods", 220.0, 160.0, false},
    {ElementType::KegCleaner,    "keg_washer",     "Keg cleaner",    150.0, 110.0, false},
    {ElementType::RecipeStation, "recipe_station", "Recipe station", 150.0, 100.0, false},
};

constexpr bool rows_match_enum_order() {
    for (std::size_t i = 0; i < std::size(kTypes); ++i) {
        if (static_cast<std::size_t>(kTypes[i].type) != i) return false;
    }
    return true;
}
static_assert(rows_match_enum_order(), "kTypes must list element types in enum order");

} // namespace

std::span<const ElementTypeInfo> element_types() {
    return kTypes;
}

const ElementTypeInfo& info(ElementType type) {
    return kTypes[static_cast<std::size_t>(type)];
}

std::optional<ElementType> element_type_from_key(std::string_view key) {
    for (const ElementTypeInfo& type : kTypes) {
        if (type.key == key) return type.type;
    }
    return std::nullopt;
}

} // namespace ninkasi::domain
