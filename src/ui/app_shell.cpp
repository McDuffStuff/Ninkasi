#include "ui/app_shell.h"

#include "app/calc_settings.h"
#include "app/unit_settings.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <exception>
#include <utility>

namespace ninkasi::ui {

AppShell::AppShell(data::Database& db, app::UserService& users, app::Session session, std::string db_path,
                   float ui_scale, std::string startup_notice)
    : settings_(db),
      session_(std::move(session)),
      personal_(db, session_.user_id),
      users_(users),
      db_path_(std::move(db_path)),
      ui_scale_(ui_scale),
      units_(app::load_unit_preferences(settings_)),
      calc_settings_(app::load_calc_settings(settings_)),
      formulas_(db, session_),
      floor_(db, session_),
      recipes_(db, session_),
      library_(db, session_),
      inventory_(db, session_),
      packaged_goods_(db, session_),
      batches_(db, session_, floor_, inventory_, packaged_goods_),
      tasks_(db, session_),
      ttb_(db, session_, settings_),
      backups_(db, session_, settings_, std::filesystem::path(db_path_)),
      exports_(db, units_),
      dialogs_(batches_, floor_, recipes_, inventory_, packaged_goods_, units_, calc_settings_, ui_scale_),
      task_views_(tasks_, floor_, batches_, ui_scale_),
      inventory_views_(inventory_, floor_, library_, units_, ui_scale_),
      packaging_views_(packaged_goods_, ui_scale_),
      element_windows_(floor_, batches_, tasks_, dialogs_, task_views_, inventory_views_, packaging_views_,
                       personal_, units_, calc_settings_, ui_scale_),
      batch_windows_(batches_, floor_, dialogs_, personal_, units_, calc_settings_, ui_scale_),
      ttb_window_(ttb_, batches_, packaged_goods_, ui_scale_),
      backup_window_(backups_, exports_, ui_scale_),
      people_window_(users_, session_, ui_scale_),
      floor_view_(floor_, batches_, tasks_, inventory_, packaged_goods_, element_windows_, dialogs_,
                  packaging_views_, personal_, units_, calc_settings_, ui_scale_),
      recipe_editors_(recipes_, library_, personal_, units_, calc_settings_, ui_scale_),
      recipe_library_(recipes_, recipe_editors_, ui_scale_),
      ingredient_window_(library_, units_, ui_scale_),
      brewing_settings_(session_, calc_settings_, settings_, recipes_, library_, formulas_, units_, ui_scale_) {
    theme_ = theme_from_name(personal_.get_or(kThemeKey, "dark"));
    apply_theme(theme_, ui_scale_);

    recipe_editors_.on_saved = [this] { recipe_library_.refresh(); };
    element_windows_.on_open_recipes = [this] { recipe_library_.show(); };
    element_windows_.on_open_batch = [this](std::int64_t batch_id) { batch_windows_.open(batch_id); };
    floor_view_.on_open_batch = [this](std::int64_t batch_id) { batch_windows_.open(batch_id); };
    floor_view_.on_open_recipes = [this] { recipe_library_.show(); };
    backup_window_.on_quit = [this] { quit_requested_ = true; };

    // The day's backup, before anything changes.
    notice_ = std::move(startup_notice);
    // Adjusted formulas that can't be used any more.
    for (const std::string& problem : formulas_.problems()) {
        notice_ += std::string(notice_.empty() ? "" : "\n\n") + problem;
    }
    // An admin who forgets their password needs a recovery code to get back in.
    const std::optional<domain::User> me = users_.person(session_.user_id);
    if (session_.is_admin() && me && me->has_password && !me->has_recovery_code) {
        notice_ += std::string(notice_.empty() ? "" : "\n\n") +
                   "You have no recovery code, so only another admin could replace a password you forget. Make one "
                   "from your name in the top right corner: New recovery code.";
    }
    try {
        backups_.back_up_if_due();
    } catch (const std::exception& e) {
        if (!notice_.empty()) notice_ += "\n\n";
        notice_ += std::string("Today's automatic backup couldn't be made. ") + e.what();
    }
}

void AppShell::files_dropped(std::vector<std::filesystem::path> files) {
    for (std::filesystem::path& file : files) dropped_files_.push_back(std::move(file));
}

void AppShell::draw() {
    if (!dropped_files_.empty()) {
        recipe_library_.import_files(dropped_files_);
        dropped_files_.clear();
    }

    draw_menu_bar();

    // The dockspace fills the main window; every other window can dock into
    // it, or be dragged outside to become its own OS window.
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (show_floor_) floor_view_.draw(dockspace_id, &show_floor_);
    element_windows_.draw();
    batch_windows_.draw();
    ttb_window_.draw();
    backup_window_.draw();
    people_window_.draw();
    task_views_.draw();
    inventory_views_.draw();
    packaging_views_.draw();
    recipe_editors_.draw();
    recipe_library_.draw();
    ingredient_window_.draw();
    brewing_settings_.draw();
    dialogs_.draw();  // last: dialogs open over whatever asked for them
    if (show_about_) draw_about_window();
    draw_notice();
    if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);
}

void AppShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (session_.is_admin()) {
            if (ImGui::MenuItem("People...")) people_window_.show();
            ImGui::SetItemTooltip("Who uses Ninkasi, their roles and passwords");
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Back up now")) {
            backup_window_.back_up_now();
            backup_window_.show();
        }
        if (ImGui::MenuItem("Backups and export...")) backup_window_.show();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) quit_requested_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Brewery Floor", nullptr, &show_floor_);
        ImGui::MenuItem("Batches", nullptr, batch_windows_.list_visible());
        ImGui::MenuItem("Tasks", nullptr, task_views_.window_visible());
        ImGui::MenuItem("Inventory", nullptr, inventory_views_.window_visible());
        ImGui::MenuItem("TTB reports", nullptr, ttb_window_.visible());
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem("Dark", nullptr, theme_ == Theme::Dark)) set_theme(Theme::Dark);
            if (ImGui::MenuItem("Light", nullptr, theme_ == Theme::Light)) set_theme(Theme::Light);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Recipes")) {
        if (ImGui::MenuItem("Recipe library")) recipe_library_.show();
        if (ImGui::MenuItem("Ingredient library")) ingredient_window_.show();
        ImGui::Separator();
        if (ImGui::MenuItem("Brewing settings")) brewing_settings_.show();
        ImGui::SetItemTooltip("Calculation formulas and the defaults for new recipes");
        if (ImGui::MenuItem("Formulas")) brewing_settings_.show_formulas();
        ImGui::SetItemTooltip("Every formula Ninkasi calculates with, written out");
        ImGui::EndMenu();
    }
    draw_units_menu();
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About Ninkasi", nullptr, &show_about_);
        ImGui::MenuItem("ImGui Demo (developer)", nullptr, &show_imgui_demo_);
        ImGui::EndMenu();
    }
    draw_account_menu();

    ImGui::EndMainMenuBar();
}

void AppShell::draw_account_menu() {
    // The signed-in person's name, right-aligned, as a menu of their own.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(session_.display_name.c_str()).x + style.ItemSpacing.x * 3.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - width);
    if (!ImGui::BeginMenu((session_.display_name + "###account").c_str())) return;
    ImGui::TextDisabled("%s", domain::name(session_.role).data());
    ImGui::Separator();
    if (ImGui::MenuItem("Change my password...")) people_window_.change_my_password();
    const std::optional<domain::User> me = users_.person(session_.user_id);
    const bool has_password = me && me->has_password;
    if (ImGui::MenuItem("New recovery code...", nullptr, false, has_password)) people_window_.new_recovery_code();
    ImGui::SetItemTooltip(has_password ? "For choosing a new password at the sign-in screen if you forget yours"
                                       : "Set a password first");
    const bool can_sign_out = users_.sign_in_needed();
    if (ImGui::MenuItem("Sign out", nullptr, false, can_sign_out)) sign_out_requested_ = true;
    if (!can_sign_out) ImGui::SetItemTooltip("With one person and no password, Ninkasi opens without signing in.");
    ImGui::EndMenu();
}

void AppShell::draw_units_menu() {
    if (!ImGui::BeginMenu("Units")) return;

    // Everyone sees the brewery's units; only admins change them.
    const bool admin = session_.is_admin();
    if (!admin) {
        ImGui::TextDisabled("Only an admin can change the units.");
        ImGui::Separator();
    }
    ImGui::BeginDisabled(!admin);
    if (ImGui::MenuItem("US brewery preset")) set_units(calc::UnitPreferences::us_brewery());
    ImGui::SetItemTooltip("Barrels, pounds, grams for small additions, °F, °Plato, SRM and °Lovibond");
    if (ImGui::MenuItem("Metric brewery preset")) set_units(calc::UnitPreferences::metric_brewery());
    ImGui::SetItemTooltip("Hectoliters, kilograms, grams for small additions, °C, °Plato and EBC");
    ImGui::Separator();

    for (calc::Measure measure : calc::kAllMeasures) {
        // "###" keeps the submenu's identity while its label shows the current unit.
        const calc::Unit current = units_.unit_for(measure);
        const std::string label = std::string(calc::name(measure)) + " (" + std::string(calc::symbol(current)) +
                                  ")###units_" + std::string(calc::key(measure));
        if (ImGui::BeginMenu(label.c_str())) {
            for (calc::Unit unit : calc::units_of(measure)) {
                if (ImGui::MenuItem(unit_label(unit).c_str(), nullptr, unit == current)) {
                    calc::UnitPreferences changed = units_;
                    changed.set(measure, unit);
                    set_units(changed);
                }
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndDisabled();
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
    personal_.set(kThemeKey, theme_name(theme_));
}

void AppShell::set_units(const calc::UnitPreferences& units) {
    units_ = units;  // the floor, element windows and recipes hold a reference to units_
    app::save_unit_preferences(session_, settings_, units_);
}

void AppShell::draw_notice() {
    constexpr const char* kPopup = "Ninkasi###notice";
    if (notice_.empty()) return;
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.0f * ui_scale_);
    ImGui::TextUnformatted(notice_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK") || dialog_enter_pressed() || dialog_escape_pressed()) {
        notice_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace ninkasi::ui
