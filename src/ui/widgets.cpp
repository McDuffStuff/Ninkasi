#include "ui/widgets.h"

#include "calc/common.h"

#include <imgui.h>

#include <cstdio>
#include <unordered_map>

namespace ninkasi::ui {

std::string unit_label(calc::Unit unit) {
    return std::string(calc::name(unit)) + " (" + std::string(calc::symbol(unit)) + ")";
}

bool input_quantity(const char* label, double& canonical_value, calc::Unit unit) {
    // ImGui hands back each keystroke's value, but the field's value is
    // recomputed from the canonical value every frame. Keep what the brewer
    // has typed here until they commit it.
    static std::unordered_map<ImGuiID, double> typed;
    const ImGuiID id = ImGui::GetID(label);

    double shown = calc::from_canonical(canonical_value, unit);
    if (!calc::is_valid(shown)) shown = 0.0;
    char format[16];
    std::snprintf(format, sizeof format, "%%.%df", calc::display_decimals(unit));

    if (ImGui::InputDouble(label, &shown, 0.0, 0.0, format)) typed[id] = shown;

    bool committed = false;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (const auto found = typed.find(id); found != typed.end()) {
            canonical_value = calc::to_canonical(found->second, unit);
            committed = true;
        }
    }
    if (ImGui::IsItemDeactivated()) typed.erase(id);

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(calc::symbol(unit).data());
    return committed;
}

} // namespace ninkasi::ui
