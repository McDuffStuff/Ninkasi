#pragma once
// Small reusable ImGui widgets for Ninkasi's data.

#include "calc/units.h"
#include "domain/recipe.h"
#include "domain/recipe_math.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <string>

namespace ninkasi::ui {

// "US beer barrel (bbl)", for unit menus.
std::string unit_label(calc::Unit unit);

// The unit for small volumes in the same system as `batch_unit`: barrels
// become gallons and hectoliters liters (e.g. for starters).
calc::Unit small_volume_unit(calc::Unit batch_unit);

// --- Committed edits ----------------------------------------------------------
// For values owned by a service: the edit is reported once, when the brewer
// presses Enter or leaves the field.

// A number field showing a canonical value in `unit` (its symbol after it).
// Returns true when an edit is committed; `canonical_value` then holds the
// new value. Use a hidden label ("##capacity") and put the visible label beside it.
bool input_quantity(const char* label, double& canonical_value, calc::Unit unit);

// --- Live edits ---------------------------------------------------------------
// For working copies (a recipe being edited): every change is written back
// at once, so results update while the brewer types. Each returns true when
// the value changed.

bool edit_quantity(const char* label, double& canonical_value, calc::Unit unit, bool show_symbol = true);
bool edit_number(const char* label, double& value, int decimals, const char* suffix = nullptr);
bool edit_percent(const char* label, double& fraction, int decimals = 1);
// A volume per mass (grain absorption, mash thickness) stored in L/kg, shown
// as qt/lb when grain is weighed in pounds or ounces.
bool edit_volume_per_mass(const char* label, double& l_per_kg, calc::Unit mass_unit);

// A combo over `values`, labelled with domain::name().
template <typename E, std::size_t N>
bool enum_combo(const char* label, E& value, const std::array<E, N>& values) {
    bool changed = false;
    if (ImGui::BeginCombo(label, domain::name(value).data())) {
        for (E option : values) {
            if (ImGui::Selectable(domain::name(option).data(), option == value) && option != value) {
                value = option;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Muted text, wrapped to the window width.
void wrapped_note(const char* text);

} // namespace ninkasi::ui
