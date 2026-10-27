#pragma once
// Equipment placed on the brewery floor ("elements").

#include "calc/units.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ninkasi::domain {

enum class ElementType {
    Brewhouse,
    Fermenter,
    BriteTank,
    DryStorage,
    ColdStorage,
    KegWasher,
    RecipeStation,
};

struct ElementTypeInfo {
    ElementType type;
    std::string_view key;   // stored in the database: never change once shipped
    std::string_view name;  // shown to the brewer (a string literal, so data() is null-terminated)
    double default_width;   // floor units
    double default_height;
    bool is_vessel;         // holds wort or beer: has a capacity, and later a batch
};

// Every element type, in the order menus show them.
std::span<const ElementTypeInfo> element_types();
const ElementTypeInfo& info(ElementType type);
std::optional<ElementType> element_type_from_key(std::string_view key);

// Position and size on the floor in floor units: one unit is one point at
// 100 % zoom.
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool operator==(const Rect&) const = default;
};

inline constexpr double kMinElementSize = 40.0;

struct Element {
    std::int64_t id = 0;
    ElementType type = ElementType::Fermenter;
    std::string name;
    Rect rect;
    std::optional<double> capacity_l;  // vessels only
    calc::UnitOverrides unit_overrides{};
};

} // namespace ninkasi::domain
