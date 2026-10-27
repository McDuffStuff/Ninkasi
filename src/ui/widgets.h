#pragma once
// Small reusable ImGui widgets for Ninkasi's data.

#include "calc/units.h"

#include <string>

namespace ninkasi::ui {

// "US beer barrel (bbl)", for unit menus.
std::string unit_label(calc::Unit unit);

// A number field that shows a canonical value in `unit` (with the unit's
// symbol after it) and lets the brewer edit it there. Returns true when an
// edit is committed (Enter, or leaving the field); `canonical_value` then
// holds the new value. Use a hidden label ("##capacity") and put the visible
// label beside it.
bool input_quantity(const char* label, double& canonical_value, calc::Unit unit);

} // namespace ninkasi::ui
