#pragma once
// Equipment placed on the brewery floor ("elements").

#include "calc/units.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

enum class ElementType {
    Brewhouse,
    Fermenter,
    BriteTank,
    Inventory,      // where ingredients and supplies are kept
    PackagedGoods,  // packaged beer: kegs and cans
    KegCleaner,     // empty kegs, dirty and clean
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
};

// A picture under the floor, such as the building's plan, drawn beneath the
// equipment. Its height follows the picture's shape.
struct FloorBackground {
    std::vector<unsigned char> image;  // the file as chosen: PNG, JPEG, BMP or GIF
    std::string file_name;
    int pixel_width = 0;
    int pixel_height = 0;
    double x = 0.0;  // its top-left corner, in floor units
    double y = 0.0;
    double width = 0.0;
    double opacity = 0.5;  // 1 is solid

    double height() const {
        return pixel_width > 0 ? width * static_cast<double>(pixel_height) / static_cast<double>(pixel_width) : 0.0;
    }
};

} // namespace ninkasi::domain
