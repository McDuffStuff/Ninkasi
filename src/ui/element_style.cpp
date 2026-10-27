#include "ui/element_style.h"

namespace ninkasi::ui {

ImVec4 element_accent(domain::ElementType type) {
    // Mid-tone colors that read on both the dark and light themes.
    switch (type) {
    case domain::ElementType::Brewhouse:     return {0.80f, 0.47f, 0.20f, 1.0f};  // copper
    case domain::ElementType::Fermenter:     return {0.30f, 0.55f, 0.82f, 1.0f};  // steel blue
    case domain::ElementType::BriteTank:     return {0.20f, 0.66f, 0.62f, 1.0f};  // teal
    case domain::ElementType::DryStorage:    return {0.72f, 0.60f, 0.38f, 1.0f};  // grain
    case domain::ElementType::ColdStorage:   return {0.42f, 0.74f, 0.92f, 1.0f};  // ice
    case domain::ElementType::KegWasher:     return {0.58f, 0.46f, 0.82f, 1.0f};  // violet
    case domain::ElementType::RecipeStation: return {0.42f, 0.68f, 0.34f, 1.0f};  // hop green
    }
    return {0.5f, 0.5f, 0.5f, 1.0f};
}

} // namespace ninkasi::ui
