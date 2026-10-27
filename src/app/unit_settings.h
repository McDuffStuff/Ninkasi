#pragma once
// The brewery-wide unit preferences, saved in the settings table as
// "units.<measure>" = "<unit key>" (e.g. units.volume = us_barrel).

#include "calc/units.h"
#include "data/settings_repository.h"

namespace ninkasi::app {

// Starts from the US brewery preset; any saved choice replaces its default.
// Unknown or mismatched saved values are ignored.
calc::UnitPreferences load_unit_preferences(data::SettingsRepository& settings);

void save_unit_preferences(data::SettingsRepository& settings, const calc::UnitPreferences& preferences);

} // namespace ninkasi::app
