#include "app/unit_settings.h"

#include <string>

namespace ninkasi::app {

namespace {

std::string settings_key(calc::Measure measure) {
    return "units." + std::string(calc::key(measure));
}

} // namespace

calc::UnitPreferences load_unit_preferences(data::SettingsRepository& settings) {
    calc::UnitPreferences preferences = calc::UnitPreferences::us_brewery();
    for (calc::Measure measure : calc::kAllMeasures) {
        const std::optional<std::string> saved = settings.get(settings_key(measure));
        if (!saved) continue;
        // set() ignores a unit that can't measure this (e.g. a volume saved for mass).
        if (const std::optional<calc::Unit> unit = calc::unit_from_key(*saved)) preferences.set(measure, *unit);
    }
    return preferences;
}

void save_unit_preferences(data::SettingsRepository& settings, const calc::UnitPreferences& preferences) {
    for (calc::Measure measure : calc::kAllMeasures) {
        settings.set(settings_key(measure), calc::key(preferences.unit_for(measure)));
    }
}

} // namespace ninkasi::app
