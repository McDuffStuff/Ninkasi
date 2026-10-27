#include "app/calc_settings.h"

#include "data/recipe_json.h"

namespace ninkasi::app {

namespace {
constexpr const char* kKey = "calc.settings";
}

domain::CalcSettings load_calc_settings(data::SettingsRepository& settings) {
    const std::optional<std::string> saved = settings.get(kKey);
    if (!saved) return {};
    try {
        return data::calc_settings_from_json(*saved);
    } catch (const data::DbError&) {
        return {};  // unreadable: fall back to the defaults rather than refuse to start
    }
}

void save_calc_settings(data::SettingsRepository& settings, const domain::CalcSettings& calc_settings) {
    settings.set(kKey, data::to_json(calc_settings));
}

} // namespace ninkasi::app
