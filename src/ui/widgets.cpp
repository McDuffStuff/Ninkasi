#include "ui/widgets.h"

#include "calc/common.h"
#include "ui/fonts.h"

#include <cstdio>
#include <unordered_map>

namespace ninkasi::ui {

namespace {

// Clicking a number selects it, so typing replaces it: quicker data entry.
constexpr ImGuiInputTextFlags kNumberFlags = ImGuiInputTextFlags_AutoSelectAll;

void format_for(char (&buffer)[16], int decimals) {
    std::snprintf(buffer, sizeof buffer, "%%.%df", decimals);
}

bool uses_imperial_mass(calc::Unit mass_unit) {
    return mass_unit == calc::Unit::Pound || mass_unit == calc::Unit::Ounce;
}

} // namespace

std::string unit_label(calc::Unit unit) {
    return std::string(calc::name(unit)) + " (" + std::string(calc::symbol(unit)) + ")";
}

calc::Unit small_volume_unit(calc::Unit batch_unit) {
    switch (batch_unit) {
    case calc::Unit::UsBarrel:   return calc::Unit::UsGallon;
    case calc::Unit::UkBarrel:   return calc::Unit::ImperialGallon;
    case calc::Unit::Hectoliter: return calc::Unit::Liter;
    default:                     return batch_unit;
    }
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
    format_for(format, calc::display_decimals(unit));

    if (ImGui::InputDouble(label, &shown, 0.0, 0.0, format, kNumberFlags)) typed[id] = shown;

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

bool edit_quantity(const char* label, double& canonical_value, calc::Unit unit, bool show_symbol) {
    double shown = calc::from_canonical(canonical_value, unit);
    if (!calc::is_valid(shown)) shown = 0.0;
    char format[16];
    format_for(format, calc::display_decimals(unit));

    const bool changed = ImGui::InputDouble(label, &shown, 0.0, 0.0, format, kNumberFlags);
    if (changed) canonical_value = calc::to_canonical(shown, unit);
    if (show_symbol) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(calc::symbol(unit).data());
    }
    return changed;
}

bool edit_number(const char* label, double& value, int decimals, const char* suffix) {
    char format[16];
    format_for(format, decimals);
    const bool changed = ImGui::InputDouble(label, &value, 0.0, 0.0, format, kNumberFlags);
    if (suffix) {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(suffix);
    }
    return changed;
}

bool edit_percent(const char* label, double& fraction, int decimals) {
    double percent = fraction * 100.0;
    char format[16];
    std::snprintf(format, sizeof format, "%%.%df %%%%", decimals);  // e.g. "%.1f %%"
    const bool changed = ImGui::InputDouble(label, &percent, 0.0, 0.0, format, kNumberFlags);
    if (changed) fraction = percent / 100.0;
    return changed;
}

bool edit_money(const char* label, double& amount, const domain::Currency& currency, float width,
                int extra_decimals) {
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::BeginGroup();
    if (!currency.symbol_after && !currency.symbol.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(currency.symbol.c_str());
        ImGui::SameLine(0.0f, gap);
    }
    char format[16];
    format_for(format, currency.decimals + extra_decimals);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputDouble(label, &amount, 0.0, 0.0, format, kNumberFlags);
    if (currency.symbol_after && !currency.symbol.empty()) {
        ImGui::SameLine(0.0f, gap);
        ImGui::TextUnformatted(currency.symbol.c_str());
    }
    ImGui::EndGroup();
    return changed;
}

bool edit_volume_per_mass(const char* label, double& l_per_kg, calc::Unit mass_unit) {
    const bool imperial = uses_imperial_mass(mass_unit);
    // 1 L/kg is 0.479 US qt/lb.
    const double per_l_per_kg = imperial ? calc::from_canonical(1.0, calc::Unit::UsQuart) /
                                               calc::from_canonical(1.0, calc::Unit::Pound)
                                         : 1.0;
    double shown = l_per_kg * per_l_per_kg;
    const bool changed = ImGui::InputDouble(label, &shown, 0.0, 0.0, "%.2f", kNumberFlags);
    if (changed) l_per_kg = shown / per_l_per_kg;
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(imperial ? "qt/lb" : "L/kg");
    return changed;
}

void wrapped_note(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

void show_recovery_code(const std::string& code) {
    ImGui::PushTextWrapPos(-1.0f);  // all on one line
    ImGui::PushFont(heading_font());
    ImGui::TextUnformatted(code.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());
    ImGui::SetItemTooltip("To paste it into a password manager");
}

void raise_os_window() {
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    // A viewport's OS window only exists from its second frame on.
    if (viewport != ImGui::GetMainViewport() && viewport->PlatformWindowCreated && platform.Platform_SetWindowFocus) {
        platform.Platform_SetWindowFocus(viewport);
    }
}

} // namespace ninkasi::ui
