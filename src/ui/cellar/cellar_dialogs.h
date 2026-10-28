#pragma once
// The brewing and cellar dialogs: start a brew, knock out (where the brewer
// confirms the ingredients actually used), transfer, package, dump, correct
// a volume, log a reading and add a note.
//
// Any screen can open one by calling the matching method from inside its
// window; it appears as a modal over that window. One is open at a time.

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

    void start_brew(std::int64_t brewhouse_id);
    // These act on the brew under way at the brewhouse.
    void knock_out(std::int64_t brewhouse_id);
    void cancel_brew(std::int64_t brewhouse_id);

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
    enum class Kind { None, StartBrew, KnockOut, CancelBrew, Transfer, Package, Dump, CorrectVolume, Reading, Note };

    void open(Kind kind);
    void close();
    // The brewery's units with an element's overrides.
    calc::UnitPreferences units_for(std::optional<std::int64_t> element_id) const;
    // Runs a service call. On success the dialog closes; on failure it stays
    // open showing the error. Returns true on success.
    bool attempt(const std::function<void()>& action);
    // The error, if any, then the confirm and dismiss buttons. Returns true
    // when the confirm button is pressed.
    bool buttons(const char* confirm, bool enabled = true, const char* dismiss = "Cancel");
    // Fermenters and brite tanks to choose from, each showing whether it can
    // take the beer and why not.
    void vessel_combo(const char* label, std::int64_t& vessel_id,
                      const std::function<app::Availability(std::int64_t)>& check, const calc::UnitPreferences& units);
    // Wrapped muted text, as wide as the dialog's fields.
    void note(const std::string& text) const;
    // What happens to the beer left behind when it is moved or packaged.
    void leftover_note(double held_l, const std::string& vessel_name, const calc::UnitPreferences& units) const;

    void draw_start_brew();
    void draw_knock_out();
    void draw_cancel_brew();
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
    std::string error_;

    // Form state. Only one dialog is open at a time, so they share it.
    std::int64_t element_id_ = 0;  // the brewhouse or vessel the dialog is about
    std::int64_t batch_id_ = 0;
    std::int64_t brew_id_ = 0;
    std::int64_t target_id_ = 0;   // the vessel beer goes into
    domain::Timestamp at_{};
    double volume_l_ = 0.0;
    bool empties_ = true;
    std::string text_;             // note or reason

    // Start brew.
    bool add_to_batch_ = false;
    std::vector<data::RecipeSummary> recipe_list_;
    std::int64_t recipe_id_ = 0;
    std::string code_;

    // Knock-out.
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
