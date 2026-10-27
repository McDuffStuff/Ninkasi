#include "ui/app_shell.h"

#include "app/unit_settings.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <utility>

namespace ninkasi::ui {

namespace {
constexpr const char* kThemeKey = "ui.theme";
}

AppShell::AppShell(data::Database& db, app::Session session, std::string db_path, float ui_scale)
    : settings_(db),
      session_(std::move(session)),
      db_path_(std::move(db_path)),
      ui_scale_(ui_scale),
      units_(app::load_unit_preferences(settings_)),
      floor_(db, session_),
      element_windows_(floor_, settings_, units_, ui_scale_),
      floor_view_(floor_, element_windows_, settings_, units_, ui_scale_) {
    theme_ = theme_from_name(settings_.get_or(kThemeKey, "dark"));
    apply_theme(theme_, ui_scale_);
}

void AppShell::draw() {
    draw_menu_bar();

    // The dockspace fills the main window; every other window can dock into
    // it, or be dragged outside to become its own OS window.
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (show_floor_) floor_view_.draw(dockspace_id, &show_floor_);
    element_windows_.draw();
    if (show_about_) draw_about_window();
    if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);
}

void AppShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", "Alt+F4")) quit_requested_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Brewery Floor", nullptr, &show_floor_);
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem("Dark", nullptr, theme_ == Theme::Dark)) set_theme(Theme::Dark);
            if (ImGui::MenuItem("Light", nullptr, theme_ == Theme::Light)) set_theme(Theme::Light);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    draw_units_menu();
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About Ninkasi", nullptr, &show_about_);
        ImGui::MenuItem("ImGui Demo (developer)", nullptr, &show_imgui_demo_);
        ImGui::EndMenu();
    }

    // Current user, right-aligned.
    const float name_width = ImGui::CalcTextSize(session_.display_name.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - name_width - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", session_.display_name.c_str());

    ImGui::EndMainMenuBar();
}

void AppShell::draw_units_menu() {
    if (!ImGui::BeginMenu("Units")) return;

    if (ImGui::MenuItem("US brewery preset")) set_units(calc::UnitPreferences::us_brewery());
    ImGui::SetItemTooltip("Barrels, pounds, °F, °Plato, SRM and °Lovibond");
    if (ImGui::MenuItem("Metric brewery preset")) set_units(calc::UnitPreferences::metric_brewery());
    ImGui::SetItemTooltip("Hectoliters, kilograms, °C, °Plato and EBC");
    ImGui::Separator();

    for (calc::Dimension dimension : calc::kAllDimensions) {
        // "###" keeps the submenu's identity while its label shows the current unit.
        const calc::Unit current = units_.unit_for(dimension);
        const std::string label = std::string(calc::name(dimension)) + " (" + std::string(calc::symbol(current)) +
                                  ")###units_" + std::string(calc::key(dimension));
        if (ImGui::BeginMenu(label.c_str())) {
            for (calc::Unit unit : calc::units_of(dimension)) {
                if (ImGui::MenuItem(unit_label(unit).c_str(), nullptr, unit == current)) {
                    calc::UnitPreferences changed = units_;
                    changed.set(unit);
                    set_units(changed);
                }
            }
            ImGui::EndMenu();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Equipment can override these in its Units tab.");
    ImGui::EndMenu();
}

void AppShell::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(420.0f * ui_scale_, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About Ninkasi", &show_about_, ImGuiWindowFlags_NoDocking)) {
        ImGui::Text("Ninkasi %s", NINKASI_VERSION);
        ImGui::TextWrapped("Lightweight, local brewery management.");
        ImGui::Separator();
        ImGui::Text("Signed in as: %s", session_.display_name.c_str());
        ImGui::TextWrapped("Database: %s", db_path_.c_str());
        ImGui::TextDisabled("Font: Roboto, Apache License 2.0");
    }
    ImGui::End();
}

void AppShell::set_theme(Theme theme) {
    theme_ = theme;
    apply_theme(theme_, ui_scale_);
    settings_.set(kThemeKey, theme_name(theme_));
}

void AppShell::set_units(const calc::UnitPreferences& units) {
    units_ = units;  // the floor and element windows hold a reference to units_
    app::save_unit_preferences(settings_, units_);
}

} // namespace ninkasi::ui
