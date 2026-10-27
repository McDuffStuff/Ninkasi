#include "domain/element.h"

#include <iterator>

namespace ninkasi::domain {

namespace {

// One row per ElementType, in enum order (checked below).
constexpr ElementTypeInfo kTypes[] = {
    {ElementType::Brewhouse,     "brewhouse",      "Brewhouse",      220.0, 140.0, true},
    {ElementType::Fermenter,     "fermenter",      "Fermenter",      120.0, 160.0, true},
    {ElementType::BriteTank,     "brite_tank",     "Brite tank",     110.0, 150.0, true},
    {ElementType::DryStorage,    "dry_storage",    "Dry storage",    200.0, 140.0, false},
    {ElementType::ColdStorage,   "cold_storage",   "Cold storage",   220.0, 160.0, false},
    {ElementType::KegWasher,     "keg_washer",     "Keg washer",     130.0, 90.0,  false},
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
