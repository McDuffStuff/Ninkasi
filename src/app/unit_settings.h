#pragma once
// The brewery-wide unit preferences, saved in the settings table as
// "units.<measure>" = "<unit key>" (e.g. units.volume = us_barrel).

#include "app/session.h"
#include "calc/units.h"
#include "data/settings_repository.h"

namespace ninkasi::app {

// Starts from the US brewery preset; any saved choice replaces its default.
// Unknown or mismatched saved values are ignored.
calc::UnitPreferences load_unit_preferences(data::SettingsRepository& settings);

// Admins only: throws NotAllowed for anyone else.
void save_unit_preferences(const Session& by, data::SettingsRepository& settings,
                           const calc::UnitPreferences& preferences);

} // namespace ninkasi::app
