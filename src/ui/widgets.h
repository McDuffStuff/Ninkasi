#pragma once
// Small reusable ImGui widgets for Ninkasi's data.

#include "calc/units.h"
#include "domain/money.h"
#include "domain/recipe.h"
#include "domain/recipe_math.h"
#include "ui/search_combo.h"

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
// An amount of money in a box `width` wide (as for SetNextItemWidth), with
// the currency's symbol beside it ("$" before it, or "€" after), so the box
// holds just the number. It's drawn as a group, so IsItemDeactivatedAfterEdit()
// and tooltips after it see the box. `extra_decimals` shows finer amounts
// than the currency's, such as a price per fl oz.
bool edit_money(const char* label, double& amount, const domain::Currency& currency, float width,
                int extra_decimals = 0);
// A volume per mass (grain absorption, mash thickness) stored in L/kg, shown
// as qt/lb when grain is weighed in pounds or ounces.
bool edit_volume_per_mass(const char* label, double& l_per_kg, calc::Unit mass_unit);

// A dropdown of `values`, labelled with domain::name(), that can be searched.
template <typename E, std::size_t N>
bool enum_combo(const char* label, E& value, const std::array<E, N>& values) {
    bool changed = false;
    if (begin_search_combo(label, domain::name(value).data())) {
        std::array<std::string, N> names;
        for (std::size_t i = 0; i < N; ++i) names[i] = std::string(domain::name(values[i]));
        for (std::size_t i : search_order(names)) {
            if (search_item(names[i].c_str(), values[i] == value) && values[i] != value) {
                value = values[i];
                changed = true;
            }
        }
        end_search_combo();
    }
    return changed;
}

// Muted text, wrapped to the window width.
void wrapped_note(const char* text);

// A recovery code in large type, with a Copy button.
void show_recovery_code(const std::string& code);

// Brings the current window's OS window to the front if it has been popped
// out of Ninkasi's main window. ImGui focuses windows inside the app but
// leaves their OS windows where they are, so without this a popped-out
// window asked to come forward could stay hidden behind the main window.
// Call between Begin() and End() in the frame the window is focused.
void raise_os_window();

} // namespace ninkasi::ui
