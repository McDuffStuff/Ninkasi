#include "app/unit_settings.h"

#include <string>

namespace ninkasi::app {

namespace {

std::string settings_key(calc::Dimension dimension) {
    return "units." + std::string(calc::key(dimension));
}

} // namespace

calc::UnitPreferences load_unit_preferences(data::SettingsRepository& settings) {
    calc::UnitPreferences preferences = calc::UnitPreferences::us_brewery();
    for (calc::Dimension dimension : calc::kAllDimensions) {
        const std::optional<std::string> saved = settings.get(settings_key(dimension));
        if (!saved) continue;
        const std::optional<calc::Unit> unit = calc::unit_from_key(*saved);
        if (unit && calc::dimension_of(*unit) == dimension) preferences.set(*unit);
    }
    return preferences;
}

void save_unit_preferences(data::SettingsRepository& settings, const calc::UnitPreferences& preferences) {
    for (calc::Dimension dimension : calc::kAllDimensions) {
        settings.set(settings_key(dimension), calc::key(preferences.unit_for(dimension)));
    }
}

} // namespace ninkasi::app
