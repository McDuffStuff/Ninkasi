#pragma once
// How times, dates and batch figures are shown, so every screen shows them
// the same way.

#include "calc/units.h"
#include "domain/batch.h"
#include "domain/timestamp.h"

#include <imgui.h>

#include <string>
#include <string_view>

namespace ninkasi::ui {

// Colors for status text, readable on the dark and light themes.
inline constexpr ImVec4 kGoodColor{0.40f, 0.75f, 0.40f, 1.0f};
inline constexpr ImVec4 kWarningColor{0.95f, 0.65f, 0.20f, 1.0f};
inline constexpr ImVec4 kErrorColor{0.92f, 0.36f, 0.32f, 1.0f};

// "2026-10-27 14:03", in local time.
std::string time_text(domain::Timestamp time);
// "2026-10-30".
std::string date_text(domain::Date date);
// "Today", "Tomorrow", "Yesterday" or the date.
std::string due_text(domain::Date due, domain::Date today);
// How long ago: "5 min", "3 h", "6 d".
std::string ago_text(domain::Timestamp then, domain::Timestamp now);

// A number with `decimals` places and a suffix, or "--" if it isn't valid.
std::string number_text(double value, int decimals, std::string_view suffix = "");

// The on-screen color of beer at `srm`.
ImVec4 beer_color(double srm);

// Text from the batch service or activity log with its quantities in `units`.
std::string rendered(std::string_view text, const calc::UnitPreferences& units);

// One label/value row of a two-column table, the label muted.
void table_row(const char* label);

} // namespace ninkasi::ui
