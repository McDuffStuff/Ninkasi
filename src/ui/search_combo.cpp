#include "ui/search_combo.h"

#include "domain/fuzzy_match.h"

#include <imgui_stdlib.h>

#include <algorithm>
#include <numeric>
#include <optional>
#include <utility>

namespace ninkasi::ui {

namespace {

// The open list's search. Only one list is open at a time.
struct Search {
    std::string text;
    bool enter = false;             // Enter was pressed in the field this frame
    bool top_pending = false;       // the next pickable item is the top match
    int shown_frame = -1;           // the last frame a list was drawn
    int enter_frame = -1;           // the last frame Enter went to a list
};

Search& search() {
    static Search state;
    return state;
}

bool enter_key_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

} // namespace

void search_field(const char* hint, float width) {
    Search& state = search();
    if (ImGui::IsWindowAppearing()) {
        state.text.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##search", hint, &state.text);
    // Escape in the field closes the list at once (the field would otherwise
    // just let go of the keyboard). Enter picks the top match; see search_item.
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    state.enter = ImGui::IsItemDeactivated() && enter_key_pressed();
    if (state.enter) state.enter_frame = ImGui::GetFrameCount();
    state.top_pending = true;
    state.shown_frame = ImGui::GetFrameCount();
}

bool begin_search_combo(const char* label, const char* preview, ImGuiComboFlags flags) {
    if (!ImGui::BeginCombo(label, preview, flags | ImGuiComboFlags_HeightLarge)) return false;
    search_field("Type to search", -FLT_MIN);
    ImGui::Separator();
    return true;
}

void end_search_combo() {
    // Enter with nothing to pick closes the list.
    Search& state = search();
    if (state.enter) {
        ImGui::CloseCurrentPopup();
        state.enter = false;
    }
    ImGui::EndCombo();
}

std::vector<std::size_t> search_order(std::span<const std::string> texts) {
    std::vector<std::size_t> order;
    if (!searching()) {
        order.resize(texts.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        return order;
    }
    std::vector<std::pair<int, std::size_t>> matches;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        if (const std::optional<int> score = domain::fuzzy_score(texts[i], search().text)) matches.emplace_back(*score, i);
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [score, index] : matches) order.push_back(index);
    return order;
}

bool searching() {
    return search().text.find_first_not_of(" \t") != std::string::npos;
}

const std::string& search_text() {
    return search().text;
}

bool search_item(const char* label, bool selected, bool enabled) {
    Search& state = search();
    const bool top = enabled && state.top_pending;
    if (top) state.top_pending = false;
    const bool highlight = searching() ? top : selected;

    ImGui::BeginDisabled(!enabled);
    bool picked = ImGui::Selectable(label, highlight);
    ImGui::EndDisabled();
    if (top && searching() && state.enter) {
        picked = true;
        state.enter = false;
        ImGui::CloseCurrentPopup();
    }
    return picked;
}

bool dialog_enter_pressed() {
    return enter_key_pressed() && search().enter_frame != ImGui::GetFrameCount();
}

bool dialog_escape_pressed() {
    // Escape closes an open list before the dialog sees it, so a list shown
    // last frame means this Escape was for the list.
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) && search().shown_frame < ImGui::GetFrameCount() - 1;
}

} // namespace ninkasi::ui
