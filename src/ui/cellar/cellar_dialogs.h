#pragma once
// The brewing and cellar dialogs: log a brew (with its knock-out, where the
// brewer confirms the ingredients actually used, or with the knock-out left
// for later), log a knock-out, transfer, package, dump, correct a volume,
// log a reading and add a note.
//
// Brewing is data entry after the fact: fill in what happened and press
// Enter. Any screen can open a dialog by calling the matching method from
// inside its window; it appears as a modal over that window. One is open at
// a time.

#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/recipe_service.h"
#include "calc/units.h"
#include "data/recipe_repository.h"
#include "domain/batch.h"
#include "domain/recipe_math.h"

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

class CellarDialogs {
public:
    CellarDialogs(app::BatchService& batches, const app::FloorService& floor, app::RecipeService& recipes,
                  const calc::UnitPreferences& units, const domain::CalcSettings& calc_settings, float ui_scale);

    // A brew on the brewhouse, with or without its knock-out.
    void log_brew(std::int64_t brewhouse_id);
    // These act on a brew whose knock-out hasn't been logged yet.
    void knock_out(std::int64_t brew_id);
    void delete_brew(std::int64_t brew_id);

    // These act on the batch in the vessel.
    void transfer(std::int64_t vessel_id);
    void package(std::int64_t vessel_id);
    void dump(std::int64_t vessel_id);
    void correct_volume(std::int64_t vessel_id);

    // A reading of a batch, taken from `vessel_id` (or wherever it is).
    void log_reading(std::int64_t batch_id, std::optional<std::int64_t> vessel_id);
    void add_note(std::optional<std::int64_t> batch_id, std::optional<std::int64_t> element_id);

    // Call once per frame, outside any window.
    void draw();

private:
    enum class Kind { None, LogBrew, KnockOut, DeleteBrew, Transfer, Package, Dump, CorrectVolume, Reading, Note };

    void open(Kind kind);
    void close();
    // The brewery's units with an element's overrides.
    calc::UnitPreferences units_for(std::optional<std::int64_t> element_id) const;
    // Runs a service call. On success the dialog closes; on failure it stays
    // open showing the error. Returns true on success.
    bool attempt(const std::function<void()>& action);
    // The error, if any, then the confirm and dismiss buttons. Returns true
    // when the confirm button is pressed, or Enter if `enter_confirms`.
    bool buttons(const char* confirm, bool enabled = true, const char* dismiss = "Cancel",
                 bool enter_confirms = true);
    // Fermenters and brite tanks to choose from, each showing whether it can
    // take the beer and why not.
    void vessel_combo(const char* label, std::int64_t& vessel_id,
                      const std::function<app::Availability(std::int64_t)>& check, const calc::UnitPreferences& units);
    // Wrapped muted text, as wide as the dialog's fields.
    void note(const std::string& text) const;
    // What happens to the beer left behind when it is moved or packaged.
    void leftover_note(double held_l, const std::string& vessel_name, const calc::UnitPreferences& units) const;

    // Knock-out defaults from a recipe: its volume, predicted OG and
    // ingredients, and the first tank that can take it. `batch_id` is the
    // batch being filled (nullopt: a new one).
    void prepare_knock_out(const domain::Recipe& recipe, std::optional<std::int64_t> batch_id);
    // The tank, volume, OG and time of a knock-out, the ingredients used and
    // notes. Returns true if they're complete enough to save.
    bool draw_knock_out_fields(const domain::Recipe& recipe, std::optional<std::int64_t> batch_id,
                               const calc::UnitPreferences& units);

    void draw_log_brew();
    void draw_knock_out();
    void draw_delete_brew();
    void draw_transfer();
    void draw_package();
    void draw_dump();
    void draw_correct_volume();
    void draw_reading();
    void draw_note();

    app::BatchService& batches_;
    const app::FloorService& floor_;
    app::RecipeService& recipes_;
    const calc::UnitPreferences& units_;
    const domain::CalcSettings& calc_settings_;
    float ui_scale_;

    Kind kind_ = Kind::None;
    bool open_requested_ = false;
    ImVec2 center_{};
    ImVec2 shown_size_{};    // the dialog's size last frame
    bool recenter_ = false;  // its size changed: center it again
    std::string error_;

    // Form state. Only one dialog is open at a time, so they share it.
    std::int64_t element_id_ = 0;  // the brewhouse or vessel the dialog is about
    std::int64_t batch_id_ = 0;
    std::int64_t brew_id_ = 0;
    std::int64_t target_id_ = 0;   // the vessel beer goes into
    domain::Timestamp at_{};       // when it happened (for brews: when the brew started)
    double volume_l_ = 0.0;
    bool empties_ = true;
    std::string text_;             // note or reason

    // Log a brew.
    bool add_to_batch_ = false;
    std::vector<data::RecipeSummary> recipe_list_;
    std::int64_t recipe_id_ = 0;
    std::optional<domain::Recipe> recipe_;  // the chosen recipe, loaded
    std::string code_;
    bool with_knock_out_ = true;
    std::string prepared_for_;              // what the knock-out defaults came from

    // Knock-out.
    domain::Timestamp knocked_out_at_{};
    bool knock_out_time_set_ = false;       // otherwise it follows the brew's start
    bool has_og_ = true;
    double og_ = 1.050;
    std::vector<domain::IngredientUse> ingredients_;

    // Package.
    domain::PackageFormat format_ = domain::PackageFormat::Kegs;

    // Reading. The method, calibration and correction factor are remembered
    // between readings.
    domain::ReadingMethod method_ = domain::ReadingMethod::Hydrometer;
    bool has_gravity_ = true;
    bool has_temperature_ = true;
    bool has_ph_ = false;
    double gravity_sg_ = 1.010;     // as read (hydrometer or digital)
    double brix_ = 6.0;             // as read (refractometer)
    double sample_temp_c_ = 20.0;
    double calibration_temp_c_ = 20.0;
    double wcf_ = 1.04;
    double temperature_c_ = 20.0;
    double ph_ = 4.4;
};

} // namespace ninkasi::ui
