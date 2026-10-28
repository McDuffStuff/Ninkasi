#include "ui/element_style.h"

namespace ninkasi::ui {

ImVec4 element_accent(domain::ElementType type) {
    // Mid-tone colors that read on both the dark and light themes.
    switch (type) {
    case domain::ElementType::Brewhouse:     return {0.80f, 0.47f, 0.20f, 1.0f};  // copper
    case domain::ElementType::Fermenter:     return {0.30f, 0.55f, 0.82f, 1.0f};  // steel blue
    case domain::ElementType::BriteTank:     return {0.20f, 0.66f, 0.62f, 1.0f};  // teal
    case domain::ElementType::Inventory:     return {0.72f, 0.60f, 0.38f, 1.0f};  // grain
    case domain::ElementType::PackagedGoods: return {0.88f, 0.70f, 0.22f, 1.0f};  // golden beer
    case domain::ElementType::KegCleaner:    return {0.58f, 0.46f, 0.82f, 1.0f};  // violet
    case domain::ElementType::RecipeStation: return {0.42f, 0.68f, 0.34f, 1.0f};  // hop green
    }
    return {0.5f, 0.5f, 0.5f, 1.0f};
}

} // namespace ninkasi::ui
