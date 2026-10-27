#pragma once
// The parts of the recipe editor. Each draws one section of a working-copy
// recipe and returns true if the brewer changed anything. The recipe editor
// uses all of them; the new-recipe defaults reuse the brewery-wide ones.

#include "app/ingredient_library.h"
#include "calc/units.h"
#include "domain/recipe.h"
#include "domain/recipe_math.h"

namespace ninkasi::ui {

struct RecipeContext {
    const calc::UnitPreferences& units;
    app::IngredientLibrary& library;
    float ui_scale;
};

// Name, style, batch size, boil time, efficiency.
bool draw_recipe_header(domain::Recipe& recipe, const RecipeContext& context);
// Batch size, boil time, efficiency and pitch rate only (for defaults).
bool draw_batch_settings(domain::Recipe& recipe, const RecipeContext& context);

// The headline numbers: OG, FG, ABV, IBU, color...
void draw_recipe_stats(const domain::RecipeStats& stats, const RecipeContext& context);

bool draw_fermentables(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context);
bool draw_hops(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context);
bool draw_yeast(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context);
bool draw_other_additions(domain::Recipe& recipe, const RecipeContext& context);
bool draw_mash(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context);
bool draw_mash_steps(domain::Recipe& recipe, const RecipeContext& context);
bool draw_water(domain::Recipe& recipe, const domain::RecipeStats& stats, const RecipeContext& context);
bool draw_water_source(domain::Recipe& recipe, const RecipeContext& context);
bool draw_equipment(domain::Recipe& recipe, const RecipeContext& context);
void draw_volumes(const domain::RecipeStats& stats, const RecipeContext& context);
bool draw_notes(domain::Recipe& recipe);

} // namespace ninkasi::ui
