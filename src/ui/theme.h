#pragma once
// Dark and light color themes for the ImGui interface.

#include <string_view>

namespace ninkasi::ui {

enum class Theme { Dark, Light };

// Applies the theme to the current ImGui style, with sizes multiplied by
// `scale` (the monitor's content scale). Requires an ImGui context.
void apply_theme(Theme theme, float scale);

// Names used when saving the theme in the settings table.
std::string_view theme_name(Theme theme);
Theme theme_from_name(std::string_view name); // unknown names give Dark

} // namespace ninkasi::ui
