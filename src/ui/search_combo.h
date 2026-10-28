#pragma once
// Dropdowns you can type in. Each dropdown's list starts with a search field
// that has the keyboard as soon as it opens: typing filters the items with a
// fuzzy match (domain::fuzzy_score), best match first, and Enter picks the
// top one.
//
//   if (begin_search_combo("##recipe", preview)) {
//       for (std::size_t i : search_order(names)) {
//           if (search_item(names[i].c_str(), ids[i] == chosen)) chosen = ids[i];
//       }
//       end_search_combo();
//   }
//
// Only one dropdown can be open at a time, so they share one search.

#include <imgui.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace ninkasi::ui {

// Like ImGui::BeginCombo, with the search field at the top of the list.
bool begin_search_combo(const char* label, const char* preview, ImGuiComboFlags flags = 0);
// Ends a list begun with begin_search_combo.
void end_search_combo();

// The search field on its own, for a popup that isn't a dropdown (the
// recipe editor's ingredient pickers). Call at the top of the popup.
void search_field(const char* hint, float width);

// The indices of `texts` that match the search, best first; all of them, in
// order, while nothing is typed.
std::vector<std::size_t> search_order(std::span<const std::string> texts);
// Whether something is typed in the search, and what.
bool searching();
const std::string& search_text();

// An item of the list, like ImGui::Selectable. While searching, the top match
// is highlighted, and Enter picks it as if it were clicked (and closes the
// list). Items that can't be picked are drawn disabled and skipped by Enter.
bool search_item(const char* label, bool selected, bool enabled = true);

// For dialogs: whether Enter or Escape was pressed for the dialog itself, and
// not to pick from, or close, a dropdown's list.
bool dialog_enter_pressed();
bool dialog_escape_pressed();

} // namespace ninkasi::ui
