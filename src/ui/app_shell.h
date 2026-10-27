#pragma once
// The top-level UI: main menu bar, dockspace, and the windows docked into it.

#include "app/floor_service.h"
#include "app/session.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/settings_repository.h"
#include "ui/elements/element_windows.h"
#include "ui/floor/floor_view.h"
#include "ui/theme.h"

#include <string>

namespace ninkasi::ui {

class AppShell {
public:
    // Requires an ImGui context; applies the saved theme immediately.
    // `ui_scale` is the monitor's content scale (1.0 at 96 DPI).
    AppShell(data::Database& db, app::Session session, std::string db_path, float ui_scale);

    // Draws one frame of UI. Call between ImGui::NewFrame() and ImGui::Render().
    void draw();

    bool quit_requested() const { return quit_requested_; }

private:
    void draw_menu_bar();
    void draw_units_menu();
    void draw_about_window();
    void set_theme(Theme theme);
    void set_units(const calc::UnitPreferences& units);

    // Declaration order matters: later members are built from earlier ones.
    data::SettingsRepository settings_;
    app::Session session_;
    std::string db_path_;
    float ui_scale_;
    Theme theme_ = Theme::Dark;
    calc::UnitPreferences units_;
    app::FloorService floor_;
    ElementWindows element_windows_;
    FloorView floor_view_;

    bool show_floor_ = true;
    bool show_about_ = false;
    bool show_imgui_demo_ = false;
    bool quit_requested_ = false;
};

} // namespace ninkasi::ui
