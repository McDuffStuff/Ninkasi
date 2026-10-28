#pragma once
// The top-level UI: main menu bar, dockspace, and the windows docked into it.

#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/ingredient_library.h"
#include "app/recipe_service.h"
#include "app/session.h"
#include "app/task_service.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/settings_repository.h"
#include "domain/recipe_math.h"
#include "ui/cellar/batch_windows.h"
#include "ui/cellar/cellar_dialogs.h"
#include "ui/elements/element_windows.h"
#include "ui/floor/floor_view.h"
#include "ui/recipes/brewing_settings_window.h"
#include "ui/recipes/ingredient_library_window.h"
#include "ui/recipes/recipe_editors.h"
#include "ui/recipes/recipe_library_window.h"
#include "ui/tasks/task_views.h"
#include "ui/theme.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ninkasi::ui {

class AppShell {
public:
    // Requires an ImGui context; applies the saved theme immediately.
    // `ui_scale` is the monitor's content scale (1.0 at 96 DPI).
    AppShell(data::Database& db, app::Session session, std::string db_path, float ui_scale);

    AppShell(const AppShell&) = delete;
    AppShell& operator=(const AppShell&) = delete;

    // Draws one frame of UI. Call between ImGui::NewFrame() and ImGui::Render().
    void draw();

    // Files dropped onto the main window; BeerJSON recipes are imported on
    // the next frame.
    void files_dropped(std::vector<std::filesystem::path> files);

    bool quit_requested() const { return quit_requested_; }

private:
    void draw_menu_bar();
    void draw_units_menu();
    void draw_about_window();
    void set_theme(Theme theme);
    void set_units(const calc::UnitPreferences& units);

    // Declaration order matters: later members are built from earlier ones,
    // and are destroyed (and save their work) first.
    data::SettingsRepository settings_;
    app::Session session_;
    std::string db_path_;
    float ui_scale_;
    Theme theme_ = Theme::Dark;
    calc::UnitPreferences units_;
    domain::CalcSettings calc_settings_;
    app::FloorService floor_;
    app::RecipeService recipes_;
    app::IngredientLibrary library_;
    app::BatchService batches_;
    app::TaskService tasks_;
    CellarDialogs dialogs_;
    TaskViews task_views_;
    ElementWindows element_windows_;
    BatchWindows batch_windows_;
    FloorView floor_view_;
    RecipeEditors recipe_editors_;
    RecipeLibraryWindow recipe_library_;
    IngredientLibraryWindow ingredient_window_;
    BrewingSettingsWindow brewing_settings_;

    std::vector<std::filesystem::path> dropped_files_;
    bool show_floor_ = true;
    bool show_about_ = false;
    bool show_imgui_demo_ = false;
    bool quit_requested_ = false;
};

} // namespace ninkasi::ui
