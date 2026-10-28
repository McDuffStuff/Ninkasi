#pragma once
// Brewing settings: the brewery's choice of formulas, the defaults new
// recipes start from (batch size, equipment, mash, water), and every formula
// written out (the Formulas tab).

#include "app/formula_service.h"
#include "app/ingredient_library.h"
#include "app/recipe_service.h"
#include "app/session.h"
#include "calc/units.h"
#include "data/settings_repository.h"
#include "domain/recipe.h"
#include "domain/recipe_math.h"
#include "ui/recipes/formulas_tab.h"

namespace ninkasi::ui {

class BrewingSettingsWindow {
public:
    // Edits `calc_settings` in place, so open recipes update immediately.
    BrewingSettingsWindow(const app::Session& session, domain::CalcSettings& calc_settings,
                          data::SettingsRepository& settings, app::RecipeService& recipes,
                          app::IngredientLibrary& library, app::FormulaService& formulas,
                          const calc::UnitPreferences& units, float ui_scale);
    ~BrewingSettingsWindow();  // saves any unsaved changes

    BrewingSettingsWindow(const BrewingSettingsWindow&) = delete;
    BrewingSettingsWindow& operator=(const BrewingSettingsWindow&) = delete;

    void show();
    // Opens the window on its Formulas tab.
    void show_formulas();
    void draw();

private:
    bool draw_calculations();
    void save();

    const app::Session& session_;
    domain::CalcSettings& calc_settings_;
    data::SettingsRepository& settings_;
    app::RecipeService& recipes_;
    app::IngredientLibrary& library_;
    const calc::UnitPreferences& units_;
    float ui_scale_;

    FormulasTab formulas_tab_;

    bool visible_ = false;
    bool focus_ = false;
    bool select_formulas_ = false;  // switch to the Formulas tab next frame
    domain::Recipe defaults_;
    bool calc_dirty_ = false;
    bool defaults_dirty_ = false;
    double last_change_ = 0.0;
};

} // namespace ninkasi::ui
