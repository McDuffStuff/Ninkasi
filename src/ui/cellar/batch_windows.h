#pragma once
// The batch list (every batch, brewing to packaged) and a window per batch
// with its figures, fermentation chart, brews, ingredients used, volume
// history and log. Open batch windows are remembered between runs.

#include "app/batch_service.h"
#include "app/floor_service.h"
#include "calc/units.h"
#include "data/batch_repository.h"
#include "data/settings_repository.h"
#include "domain/activity.h"
#include "domain/batch.h"
#include "domain/recipe_math.h"
#include "ui/cellar/cellar_dialogs.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ninkasi::ui {

class BatchWindows {
public:
    BatchWindows(app::BatchService& batches, const app::FloorService& floor, CellarDialogs& dialogs,
                 data::SettingsRepository& settings, const calc::UnitPreferences& units,
                 const domain::CalcSettings& calc_settings, float ui_scale);

    void show_list();
    bool* list_visible() { return &list_visible_; }
    // Opens a batch's window, or brings it to the front.
    void open(std::int64_t batch_id);

    // Call once per frame.
    void draw();

private:
    struct Window {
        std::int64_t batch_id = 0;
        bool focus = false;
        // What's read from the database, refreshed when batches change.
        bool loaded = false;
        std::uint64_t revision = 0;
        std::optional<domain::Batch> finished;  // a batch no longer in memory
        std::vector<domain::Activity> log;
        std::vector<domain::VolumeEvent> ledger;
        std::unordered_map<std::int64_t, std::vector<domain::IngredientUse>> used;  // by brew
    };

    void draw_list();
    // Returns false once the brewer closes the window.
    bool draw_window(Window& window);
    void refresh(Window& window);
    void draw_overview(const domain::Batch& batch, const domain::RecipeStats& predicted);
    void draw_brews(const domain::Batch& batch);
    void draw_ledger(const Window& window);
    void draw_recipe(const domain::Batch& batch, const domain::RecipeStats& predicted);
    // "Fermenter 2" or "Fermenter 2 (9.60 bbl)"; "" if it's in no vessel.
    std::string where_text(std::int64_t batch_id, bool with_volumes) const;
    std::string element_name(std::optional<std::int64_t> element_id) const;
    void save_open_windows();

    app::BatchService& batches_;
    const app::FloorService& floor_;
    CellarDialogs& dialogs_;
    data::SettingsRepository& settings_;
    const calc::UnitPreferences& units_;
    const domain::CalcSettings& calc_settings_;
    float ui_scale_;

    bool list_visible_ = false;
    bool list_focus_ = false;
    bool show_finished_ = true;
    std::string filter_;
    std::vector<data::BatchSummary> list_;
    std::uint64_t list_revision_ = 0;
    bool list_loaded_ = false;

    std::vector<Window> windows_;
};

} // namespace ninkasi::ui
