#pragma once
// Brewing settings: the brewery's choice of formulas, and the defaults new
// recipes start from (batch size, equipment, mash, water).

#include "app/ingredient_library.h"
#include "app/recipe_service.h"
#include "calc/units.h"
#include "data/settings_repository.h"
#include "domain/recipe.h"
#include "domain/recipe_math.h"

namespace ninkasi::ui {

class BrewingSettingsWindow {
public:
    // Edits `calc_settings` in place, so open recipes update immediately.
    BrewingSettingsWindow(domain::CalcSettings& calc_settings, data::SettingsRepository& settings,
                          app::RecipeService& recipes, app::IngredientLibrary& library,
                          const calc::UnitPreferences& units, float ui_scale);
    ~BrewingSettingsWindow();  // saves any unsaved changes

    BrewingSettingsWindow(const BrewingSettingsWindow&) = delete;
    BrewingSettingsWindow& operator=(const BrewingSettingsWindow&) = delete;

    void show();
    void draw();

private:
    bool draw_calculations();
    void save();

    domain::CalcSettings& calc_settings_;
    data::SettingsRepository& settings_;
    app::RecipeService& recipes_;
    app::IngredientLibrary& library_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    domain::Recipe defaults_;
    bool calc_dirty_ = false;
    bool defaults_dirty_ = false;
    double last_change_ = 0.0;
};

} // namespace ninkasi::ui
