#pragma once
// Dark and light color themes for the ImGui interface, and the text size.

#include <array>
#include <string>
#include <string_view>

namespace ninkasi::ui {

enum class Theme { Dark, Light };

// Where each person's theme is saved, in their own settings.
inline constexpr const char* kThemeKey = "ui.theme";

// Applies the theme to the current ImGui style, with sizes multiplied by
// `scale` (the monitor's content scale). Requires an ImGui context.
void apply_theme(Theme theme, float scale);

// Names used when saving the theme in the settings table.
std::string_view theme_name(Theme theme);
Theme theme_from_name(std::string_view name); // unknown names give Dark

// How big text is drawn, as a share of normal: each person's choice, saved in
// their own settings. Everything else (spacing, field widths) grows with it,
// so nothing gets cut off.
inline constexpr const char* kTextSizeKey = "ui.text_size";
inline constexpr std::array kTextSizes = {0.85f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f};

// The size saved as `saved` ("1.15"): the nearest of kTextSizes, or 1 if it
// isn't a number.
float text_size_from_setting(std::string_view saved);
std::string text_size_setting(float size);

} // namespace ninkasi::ui
