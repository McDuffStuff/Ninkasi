#pragma once
// Detail windows for floor equipment. Each can be docked, left floating,
// popped out into its own OS window (so several screens can be used at
// once), or maximized. The open windows are remembered between runs.
//
// Tanks show the batch they hold with its readings, a brewhouse logs brews
// and lists those waiting for their knock-out, and every piece of equipment
// has its tasks and log.

#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/task_service.h"
#include "calc/units.h"
#include "data/batch_repository.h"
#include "data/settings_repository.h"
#include "domain/activity.h"
#include "domain/element.h"
#include "domain/recipe_math.h"
#include "ui/cellar/cellar_dialogs.h"
#include "ui/inventory/inventory_views.h"
#include "ui/tasks/task_views.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class ElementWindows {
public:
    ElementWindows(app::FloorService& floor, app::BatchService& batches, const app::TaskService& tasks,
                   CellarDialogs& dialogs, TaskViews& task_views, InventoryViews& inventory_views,
                   data::SettingsRepository& settings,
                   const calc::UnitPreferences& units, const domain::CalcSettings& calc_settings, float ui_scale);

    // Opens the element's window, or brings it to the front if already open.
    void open(std::int64_t element_id);
    void draw();

    // Called by a recipe station's "Open recipe library" button.
    std::function<void()> on_open_recipes;
    // Called by "Open batch" buttons.
    std::function<void(std::int64_t batch_id)> on_open_batch;

private:
    struct Window {
        std::int64_t element_id = 0;
        bool focus = false;       // bring to the front next frame
        bool popped_out = false;  // in its own OS window
        bool undock = false;      // leave the dockspace next frame
        bool maximized = false;
        bool restore = false;     // return to restore_pos/size next frame
        ImVec2 restore_pos{};
        ImVec2 restore_size{};
        ImVec2 maximized_pos{};
        ImVec2 maximized_size{};
        bool editing_name = false;
        std::string name_edit;

        // The log and brew history, re-read when batches or tasks change.
        bool loaded = false;
        std::uint64_t batch_revision = 0;
        std::uint64_t task_revision = 0;
        std::vector<domain::Activity> log;
        std::vector<data::BrewSummary> history;
    };

    // Returns false once the brewer closes the window.
    bool draw_window(Window& window, const domain::Element& element);
    void draw_window_controls(Window& window, const domain::Element& element);
    void draw_overview_tab(Window& window, const domain::Element& element);
    void draw_equipment_settings(Window& window, const domain::Element& element);
    void draw_vessel_status(const domain::Element& element, const calc::UnitPreferences& units);
    void draw_brewhouse_status(const domain::Element& element);
    void draw_history_tab(const Window& window, const calc::UnitPreferences& units);
    void draw_units_tab(const domain::Element& element);
    void refresh(Window& window);
    void maximize(Window& window);
    void save_open_windows();

    app::FloorService& floor_;
    app::BatchService& batches_;
    const app::TaskService& tasks_;
    CellarDialogs& dialogs_;
    TaskViews& task_views_;
    InventoryViews& inventory_views_;
    data::SettingsRepository& settings_;
    const calc::UnitPreferences& units_;
    const domain::CalcSettings& calc_settings_;
    float ui_scale_;
    std::vector<Window> windows_;
};

} // namespace ninkasi::ui
