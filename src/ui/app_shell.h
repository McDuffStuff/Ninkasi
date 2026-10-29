#pragma once
// The top-level UI: main menu bar, dockspace, and the windows docked into it.

#include "app/backup_service.h"
#include "app/batch_service.h"
#include "app/costing_service.h"
#include "app/expense_service.h"
#include "app/export_service.h"
#include "app/floor_service.h"
#include "app/formula_service.h"
#include "app/insights_service.h"
#include "app/ingredient_library.h"
#include "app/inventory_service.h"
#include "app/packaged_goods_service.h"
#include "app/recipe_service.h"
#include "app/sales_service.h"
#include "app/session.h"
#include "app/task_service.h"
#include "app/ttb_service.h"
#include "app/user_service.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/settings_repository.h"
#include "domain/recipe_math.h"
#include "ui/backup_window.h"
#include "ui/cellar/batch_windows.h"
#include "ui/cellar/cellar_dialogs.h"
#include "ui/elements/element_windows.h"
#include "ui/floor/floor_view.h"
#include "ui/insights/insights_window.h"
#include "ui/inventory/inventory_views.h"
#include "ui/packaging/packaging_views.h"
#include "ui/people_window.h"
#include "ui/recipes/brewing_settings_window.h"
#include "ui/recipes/ingredient_library_window.h"
#include "ui/recipes/recipe_editors.h"
#include "ui/recipes/recipe_library_window.h"
#include "ui/sales/customers_window.h"
#include "ui/sales/expenses_window.h"
#include "ui/sales/pos_import_window.h"
#include "ui/sales/price_list_window.h"
#include "ui/sales/sale_dialog.h"
#include "ui/sales/sales_history_window.h"
#include "ui/sales/takings_dialog.h"
#include "ui/tasks/task_views.h"
#include "ui/theme.h"
#include "ui/ttb/ttb_window.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ninkasi::ui {

class AppShell {
public:
    // Requires an ImGui context; applies the person's theme immediately.
    // `session` is whoever signed in. `ui_scale` is the monitor's content
    // scale (1.0 at 96 DPI) times the person's text size. A `startup_notice`
    // (a backup restored, say) is shown when it opens, and with `greet` so are
    // reminders for the person (an admin without a recovery code...); without
    // it, the shell is being built again after a change of text size. Makes
    // the day's automatic backup if one is due.
    AppShell(data::Database& db, app::UserService& users, app::Session session, std::string db_path,
             float ui_scale, std::string startup_notice = {}, bool greet = true);

    AppShell(const AppShell&) = delete;
    AppShell& operator=(const AppShell&) = delete;

    // Draws one frame of UI. Call between ImGui::NewFrame() and ImGui::Render().
    void draw();

    // Files dropped onto the main window; BeerJSON recipes are imported on
    // the next frame.
    void files_dropped(std::vector<std::filesystem::path> files);

    bool quit_requested() const { return quit_requested_; }
    // The person chose Sign out: close the shell and show the sign-in screen.
    bool sign_out_requested() const { return sign_out_requested_; }
    // The person changed their text size: build the interface again at the new size.
    bool rebuild_requested() const { return rebuild_requested_; }

private:
    void draw_menu_bar();
    void draw_units_menu();
    void draw_sales_menu();
    void draw_account_menu();
    void draw_about_window();
    void draw_notice();
    void set_theme(Theme theme);
    void draw_text_size_menu();
    // Saves the person's text size; the interface is built again at it.
    void set_text_size(float size);
    // One step bigger (+1) or smaller (-1).
    void step_text_size(int steps);
    void set_units(const calc::UnitPreferences& units);

    // Declaration order matters: later members are built from earlier ones,
    // and are destroyed (and save their work) first.
    data::SettingsRepository settings_;  // the brewery's
    app::Session session_;
    data::SettingsRepository personal_;  // the signed-in person's: theme, open windows, floor view
    app::UserService& users_;
    std::string db_path_;
    float ui_scale_;
    Theme theme_ = Theme::Dark;
    float text_size_ = 1.0f;
    calc::UnitPreferences units_;
    domain::CalcSettings calc_settings_;
    app::FormulaService formulas_;  // first, so adjusted formulas are in place before anything calculates
    app::FloorService floor_;
    app::RecipeService recipes_;
    app::IngredientLibrary library_;
    app::InventoryService inventory_;
    app::PackagedGoodsService packaged_goods_;
    app::BatchService batches_;
    app::TaskService tasks_;
    app::TtbService ttb_;
    app::SalesService sales_;
    app::ExpenseService expenses_;
    app::CostingService costing_;
    app::InsightsService insights_;
    app::BackupService backups_;
    app::ExportService exports_;
    CellarDialogs dialogs_;
    TaskViews task_views_;
    InventoryViews inventory_views_;
    PackagingViews packaging_views_;
    ElementWindows element_windows_;
    BatchWindows batch_windows_;
    TtbWindow ttb_window_;
    BackupWindow backup_window_;
    PeopleWindow people_window_;
    CustomersWindow customers_window_;
    PriceListWindow price_list_window_;
    SaleDialog sale_dialog_;
    SalesHistoryWindow sales_history_;
    TakingsDialog takings_dialog_;
    PosImportWindow pos_import_window_;
    ExpensesWindow expenses_window_;
    InsightsWindow insights_window_;
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
    bool sign_out_requested_ = false;
    bool rebuild_requested_ = false;
    std::string notice_;  // shown once, then cleared
};

} // namespace ninkasi::ui
