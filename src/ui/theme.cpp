#include "ui/theme.h"

#include <imgui.h>

namespace ninkasi::ui {

namespace {

// Amber accent, a nod to the beer. Kept in one place so a user-selectable
// accent color can replace it later.
constexpr ImVec4 kAccent{0.85f, 0.55f, 0.15f, 1.00f};

ImVec4 with_alpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

// ImGui's dark theme tints frames and title bars blue; neutral grays let the
// accent and the equipment colors stand out instead.
void apply_dark_neutrals(ImGuiStyle& style) {
    ImVec4* c = style.Colors;
    c[ImGuiCol_FrameBg]          = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.09f, 0.10f, 0.75f);
    c[ImGuiCol_ResizeGrip]       = ImVec4(0.40f, 0.40f, 0.44f, 0.25f);
}

void apply_accent(ImGuiStyle& style) {
    ImVec4* c = style.Colors;
    c[ImGuiCol_FrameBgHovered]       = with_alpha(kAccent, 0.25f);
    c[ImGuiCol_FrameBgActive]        = with_alpha(kAccent, 0.40f);
    c[ImGuiCol_SeparatorHovered]     = with_alpha(kAccent, 0.70f);
    c[ImGuiCol_SeparatorActive]      = kAccent;
    c[ImGuiCol_TabDimmed]            = with_alpha(kAccent, 0.12f);
    c[ImGuiCol_TabDimmedSelected]    = with_alpha(kAccent, 0.30f);
    c[ImGuiCol_TabSelectedOverline]  = kAccent;
    c[ImGuiCol_CheckMark]            = kAccent;
    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = kAccent;
    c[ImGuiCol_Button]               = with_alpha(kAccent, 0.45f);
    c[ImGuiCol_ButtonHovered]        = with_alpha(kAccent, 0.75f);
    c[ImGuiCol_ButtonActive]         = kAccent;
    c[ImGuiCol_Header]               = with_alpha(kAccent, 0.35f);
    c[ImGuiCol_HeaderHovered]        = with_alpha(kAccent, 0.60f);
    c[ImGuiCol_HeaderActive]         = kAccent;
    c[ImGuiCol_Tab]                  = with_alpha(kAccent, 0.25f);
    c[ImGuiCol_TabHovered]           = with_alpha(kAccent, 0.70f);
    c[ImGuiCol_TabSelected]          = with_alpha(kAccent, 0.55f);
    c[ImGuiCol_ResizeGripHovered]    = with_alpha(kAccent, 0.70f);
    c[ImGuiCol_ResizeGripActive]     = kAccent;
    c[ImGuiCol_DockingPreview]       = with_alpha(kAccent, 0.60f);
    c[ImGuiCol_TextSelectedBg]       = with_alpha(kAccent, 0.35f);
    c[ImGuiCol_NavCursor]            = kAccent;
}

} // namespace

void apply_theme(Theme theme, float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();  // start from defaults so sizes are never scaled twice
    if (theme == Theme::Light) {
        ImGui::StyleColorsLight(&style);
    } else {
        ImGui::StyleColorsDark(&style);
        apply_dark_neutrals(style);
    }
    apply_accent(style);

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    // Popped-out windows become real OS windows; square, opaque corners keep
    // them from looking broken against the desktop.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    style.ScaleAllSizes(scale);
}

std::string_view theme_name(Theme theme) {
    return theme == Theme::Light ? "light" : "dark";
}

Theme theme_from_name(std::string_view name) {
    return name == "light" ? Theme::Light : Theme::Dark;
}

} // namespace ninkasi::ui
