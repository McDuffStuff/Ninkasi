#pragma once
// The brewery's calculation choices (IBU formula, color model...), saved in
// the settings table.

#include "data/settings_repository.h"
#include "domain/recipe_math.h"

namespace ninkasi::app {

// Defaults if nothing has been saved yet, or the saved value is unreadable.
domain::CalcSettings load_calc_settings(data::SettingsRepository& settings);
void save_calc_settings(data::SettingsRepository& settings, const domain::CalcSettings& calc_settings);

} // namespace ninkasi::app
