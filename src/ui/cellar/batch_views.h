#pragma once
// Parts of the batch screens shared by vessel windows and batch windows:
// headline figures, the fermentation chart, readings, ingredients used and
// the activity log.

#include "calc/alcohol.h"
#include "calc/units.h"
#include "domain/activity.h"
#include "domain/batch.h"
#include "domain/recipe_math.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ninkasi::ui {

// "brewed 6 days ago", "brewed today", or before the knock-out is logged
// "brewed 2026-10-27, knock-out to log".
std::string batch_age_text(const domain::Batch& batch);

// The unit an ingredient's amount is shown in, or nullopt for packs and
// free-text amounts.
std::optional<calc::Unit> amount_unit(const domain::IngredientUse& use, const calc::UnitPreferences& units);
// An amount of an ingredient with its unit: "4.5 lb", "2 packs", "1 tablet".
std::string amount_text(const domain::IngredientUse& use, double amount, const calc::UnitPreferences& units);

// OG, latest gravity, ABV and attenuation so far, temperature and the
// recipe's predicted FG, in one row.
void draw_batch_figures(const domain::Batch& batch, const domain::RecipeStats& predicted,
                        const calc::UnitPreferences& units, calc::AbvFormula formula);

// Gravity and temperature over time, with the predicted FG as a line.
void draw_fermentation_chart(const domain::Batch& batch, double predicted_fg, const calc::UnitPreferences& units,
                             float height);

// The readings, newest first, each with a delete button. Returns the id of
// a reading the brewer chose to delete.
std::optional<std::int64_t> draw_readings_table(const domain::Batch& batch, const calc::UnitPreferences& units);

// Ingredients a brew used, planned versus actual.
void draw_ingredients_used(const std::vector<domain::IngredientUse>& uses, const calc::UnitPreferences& units);

// Activity entries, newest first.
void draw_activity(const std::vector<domain::Activity>& entries, const calc::UnitPreferences& units);

} // namespace ninkasi::ui
