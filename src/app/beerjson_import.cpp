#include "app/beerjson_import.h"

#include "calc/extract.h"
#include "calc/gravity.h"
#include "calc/units.h"
#include "calc/yeast.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>

namespace ninkasi::app {

namespace {

using nlohmann::json;
using domain::FermentableType;

// BeerJSON measurements look like {"value": 20, "unit": "l"}.
struct UnitFactor {
    std::string_view unit;
    double factor;  // to Ninkasi's canonical unit
};

constexpr UnitFactor kLiters[] = {
    {"ml", 0.001},          {"l", 1.0},
    {"tsp", 0.00492892159375}, {"tbsp", 0.01478676478125},
    {"floz", 0.0295735295625}, {"cup", 0.2365882365},
    {"pt", 0.473176473},    {"qt", 0.946352946},
    {"gal", 3.785411784},   {"bbl", 117.347765304},
    {"ifloz", 0.0284130625}, {"ipt", 0.56826125},
    {"iqt", 1.1365225},     {"igal", 4.54609},
    {"ibbl", 163.65924},
};
constexpr UnitFactor kKilograms[] = {
    {"mg", 1.0e-6}, {"g", 0.001}, {"kg", 1.0}, {"lb", 0.45359237}, {"oz", 0.028349523125},
};
constexpr UnitFactor kMinutes[] = {
    {"sec", 1.0 / 60.0}, {"min", 1.0}, {"hr", 60.0}, {"day", 1440.0}, {"week", 10080.0},
};
constexpr std::string_view kCountUnits[] = {"1", "unit", "each", "dimensionless", "pkg"};

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool contains_ignoring_case(const std::string& text, std::string_view word) {
    return lowercase(text).find(word) != std::string::npos;
}

// Reads one recipe, noting anything skipped or assumed.
class RecipeReader {
public:
    RecipeReader(std::vector<std::string>& warnings, const domain::Recipe& defaults)
        : warnings_(warnings), defaults_(defaults) {}

    domain::Recipe read(const json& r);

private:
    void warn(const std::string& message) { warnings_.push_back(recipe_name_ + ": " + message); }

    static const json* object(const json& j, const char* field) {
        const auto found = j.find(field);
        return (found != j.end() && found->is_object()) ? &*found : nullptr;
    }

    static std::string text(const json& j, const char* field, std::string fallback = {}) {
        const auto found = j.find(field);
        return (found != j.end() && found->is_string()) ? found->get<std::string>() : fallback;
    }

    // The {"value", "unit"} at `field`, if it is one.
    static std::optional<std::pair<double, std::string>> measurement(const json& j, const char* field) {
        const json* m = object(j, field);
        if (!m) return std::nullopt;
        const auto value = m->find("value");
        const auto unit = m->find("unit");
        if (value == m->end() || !value->is_number() || unit == m->end() || !unit->is_string()) return std::nullopt;
        return std::pair{value->get<double>(), unit->get<std::string>()};
    }

    // A measurement converted with `table`, if its unit is in the table.
    // Amounts can be a mass or a volume, so trying one table stays quiet;
    // warn_about_amount() reports an amount nothing could read.
    std::optional<double> scaled(const json& j, const char* field, std::span<const UnitFactor> table,
                                 bool warn_unknown = true) {
        const auto m = measurement(j, field);
        if (!m) return std::nullopt;
        const std::string unit = lowercase(m->second);
        for (const UnitFactor& entry : table) {
            if (entry.unit == unit) return m->first * entry.factor;
        }
        if (warn_unknown) warn(std::string("unknown unit \"") + m->second + "\" for " + field);
        return std::nullopt;
    }

    std::optional<double> liters(const json& j, const char* field) { return scaled(j, field, kLiters); }
    std::optional<double> minutes(const json& j, const char* field) { return scaled(j, field, kMinutes); }
    std::optional<double> amount_kg(const json& j) { return scaled(j, "amount", kKilograms, false); }
    std::optional<double> amount_l(const json& j) { return scaled(j, "amount", kLiters, false); }

    void warn_about_amount(const json& j, const std::string& ingredient) {
        if (const auto m = measurement(j, "amount")) {
            warn(ingredient + " has an amount in an unknown unit (\"" + m->second + "\")");
        } else {
            warn(ingredient + " has no amount");
        }
    }

    std::optional<double> fraction(const json& j, const char* field) {
        const auto m = measurement(j, field);
        if (!m) return std::nullopt;
        if (m->second != "%") warn(std::string("expected % for ") + field + ", got \"" + m->second + "\"");
        return m->first / 100.0;
    }

    std::optional<double> celsius(const json& j, const char* field) {
        const auto m = measurement(j, field);
        if (!m) return std::nullopt;
        if (m->second == "C") return m->first;
        if (m->second == "F") return calc::to_canonical(m->first, calc::Unit::Fahrenheit);
        warn("unknown temperature unit \"" + m->second + "\"");
        return std::nullopt;
    }

    std::optional<double> specific_gravity(const json& j, const char* field) {
        const auto m = measurement(j, field);
        if (!m) return std::nullopt;
        const std::string unit = lowercase(m->second);
        if (unit == "sg") return m->first;
        if (unit == "plato" || unit == "brix") return calc::plato_to_sg(m->first);
        warn("unknown gravity unit \"" + m->second + "\"");
        return std::nullopt;
    }

    // Malt color in °Lovibond (SRM is treated as equal).
    std::optional<double> lovibond(const json& j, const char* field) {
        const auto m = measurement(j, field);
        if (!m) return std::nullopt;
        if (m->second == "Lovi" || m->second == "SRM") return m->first;
        if (m->second == "EBC") return calc::to_canonical(m->first, calc::Unit::Ebc);
        warn("unknown color unit \"" + m->second + "\"");
        return std::nullopt;
    }

    bool is_count(const json& j, const char* field) {
        const auto m = measurement(j, field);
        return m && std::find(std::begin(kCountUnits), std::end(kCountUnits), lowercase(m->second)) !=
                        std::end(kCountUnits);
    }

    domain::FermentableAddition read_fermentable(const json& f);
    domain::HopAddition read_hop(const json& h, double boil_time_min);
    domain::YeastAddition read_culture(const json& c, domain::Recipe& recipe);
    domain::MiscAddition read_misc(const json& m);
    domain::MashStep read_mash_step(const json& s);

    std::vector<std::string>& warnings_;
    const domain::Recipe& defaults_;
    std::string recipe_name_;
};

domain::Recipe RecipeReader::read(const json& r) {
    domain::Recipe recipe = defaults_;
    recipe.id = 0;
    recipe.style.clear();
    recipe.author.clear();
    recipe.notes.clear();
    recipe.fermentables.clear();
    recipe.hops.clear();
    recipe.yeasts.clear();
    recipe.miscs.clear();
    recipe.water.salts.clear();
    recipe.water.mash_acids.clear();
    recipe.starter = {};

    recipe.name = text(r, "name", "Imported recipe");
    recipe_name_ = recipe.name;
    recipe.author = text(r, "author");
    recipe.notes = text(r, "notes");
    if (const json* style = object(r, "style")) recipe.style = text(*style, "name");

    if (const auto batch = liters(r, "batch_size")) {
        recipe.batch_volume_l = *batch;
    } else {
        warn("no batch size; used the default");
    }
    if (const json* efficiency = object(r, "efficiency")) {
        if (const auto brewhouse = fraction(*efficiency, "brewhouse")) recipe.efficiency = *brewhouse;
    }
    if (const json* boil = object(r, "boil")) {
        if (const auto time = minutes(*boil, "boil_time")) recipe.boil_time_min = *time;
    }

    if (const json* ingredients = object(r, "ingredients")) {
        auto each = [&](const char* field, auto read) {
            const auto found = ingredients->find(field);
            if (found == ingredients->end() || !found->is_array()) return;
            for (const json& item : *found) {
                if (item.is_object()) read(item);
            }
        };
        each("fermentable_additions", [&](const json& f) { recipe.fermentables.push_back(read_fermentable(f)); });
        each("hop_additions", [&](const json& h) { recipe.hops.push_back(read_hop(h, recipe.boil_time_min)); });
        each("culture_additions", [&](const json& c) { recipe.yeasts.push_back(read_culture(c, recipe)); });
        each("miscellaneous_additions", [&](const json& m) { recipe.miscs.push_back(read_misc(m)); });
        if (ingredients->contains("water_additions")) {
            warn("water profiles aren't imported; check the Water tab");
        }
    }

    if (const json* mash = object(r, "mash")) {
        const auto steps = mash->find("mash_steps");
        if (steps != mash->end() && steps->is_array() && !steps->empty()) {
            recipe.mash_steps.clear();
            for (const json& step : *steps) {
                if (step.is_object()) recipe.mash_steps.push_back(read_mash_step(step));
            }
        }
    }
    return recipe;
}

domain::FermentableAddition RecipeReader::read_fermentable(const json& f) {
    domain::FermentableAddition addition;
    domain::Fermentable& spec = addition.fermentable;
    spec.name = text(f, "name", "Fermentable");
    spec.supplier = text(f, "producer");

    const std::string type = text(f, "type", "grain");
    if (type == "grain") spec.type = FermentableType::Grain;
    else if (type == "dry extract") spec.type = FermentableType::DryExtract;
    else if (type == "extract") spec.type = FermentableType::Extract;
    else if (type == "sugar" || type == "honey") spec.type = FermentableType::Sugar;
    else if (type == "fruit" || type == "juice") spec.type = FermentableType::Fruit;
    else spec.type = FermentableType::Other;

    const std::string group = text(f, "grain_group");
    if (group == "caramel") spec.category = calc::MaltCategory::Crystal;
    else if (group == "roasted") spec.category = calc::MaltCategory::Roasted;
    if ((group == "flaked" || group == "adjunct") && spec.type == FermentableType::Grain) {
        spec.type = FermentableType::Adjunct;
    }
    if (spec.type == FermentableType::Grain && contains_ignoring_case(spec.name, "acid")) {
        spec.category = calc::MaltCategory::Acidulated;
    }

    if (const auto color = lovibond(f, "color")) {
        spec.color_lovibond = *color;
    } else {
        warn(spec.name + " has no color; used 0");
        spec.color_lovibond = 0.0;
    }

    // Prefer the potential gravity (as-is, homebrew style); fall back to the
    // laboratory fine-grind or coarse-grind yield.
    bool has_yield = false;
    if (const json* yield = object(f, "yield")) {
        if (const auto potential = specific_gravity(*yield, "potential")) {
            spec.yield = calc::yield_from_potential_sg(*potential);
            has_yield = true;
        } else if (const auto fine = fraction(*yield, "fine_grind")) {
            spec.yield = *fine;
            has_yield = true;
        } else if (const auto coarse = fraction(*yield, "coarse_grind")) {
            spec.yield = *coarse;
            has_yield = true;
        }
    }
    if (!has_yield) warn(spec.name + " has no yield; used " + std::to_string(static_cast<int>(spec.yield * 100)) + " %");

    if (const auto kg = amount_kg(f)) {
        addition.mass_kg = *kg;
    } else if (const auto l = amount_l(f)) {
        const bool syrup = spec.type == FermentableType::Extract || type == "honey";
        const double density = syrup ? 1.42 : 1.0;
        addition.mass_kg = *l * density;
        char note[160];
        std::snprintf(note, sizeof note, "%s is measured by volume; assumed %.2f kg/L", spec.name.c_str(), density);
        warn(note);
    } else {
        warn_about_amount(f, spec.name);
    }
    return addition;
}

domain::HopAddition RecipeReader::read_hop(const json& h, double boil_time_min) {
    domain::HopAddition addition;
    addition.hop.name = text(h, "name", "Hop");
    addition.hop.origin = text(h, "origin");

    const std::string form = text(h, "form", "pellet");
    if (form == "leaf" || form == "leaf (wet)") addition.hop.form = calc::HopForm::Leaf;
    else if (form == "plug") addition.hop.form = calc::HopForm::Plug;
    else if (form == "powder") addition.hop.form = calc::HopForm::Powder;
    else if (form == "extract") addition.hop.form = calc::HopForm::Extract;
    else addition.hop.form = calc::HopForm::Pellet;

    if (const auto alpha = fraction(h, "alpha_acid")) {
        addition.hop.alpha_acid = *alpha;
    } else {
        warn(addition.hop.name + " has no alpha acid");
        addition.hop.alpha_acid = 0.0;
    }

    if (const auto kg = amount_kg(h)) {
        addition.mass_kg = *kg;
    } else if (const auto l = amount_l(h)) {
        addition.mass_kg = *l;  // hop extract: roughly 1 kg/L
        warn(addition.hop.name + " is measured by volume; assumed 1 kg/L");
    } else {
        warn_about_amount(h, addition.hop.name);
    }

    addition.use = calc::HopUse::Boil;
    addition.time_min = boil_time_min;
    if (const json* timing = object(h, "timing")) {
        const std::string use = text(*timing, "use", "add_to_boil");
        const std::optional<double> time = minutes(*timing, "time");
        const std::optional<double> duration = minutes(*timing, "duration");
        if (use == "add_to_mash") {
            addition.use = calc::HopUse::Mash;
        } else if (use == "add_to_fermentation" || use == "add_to_package") {
            addition.use = calc::HopUse::DryHop;
            addition.time_min = 0.0;
        } else if (time && *time <= 0.0 && duration && *duration > 0.0) {
            // Added at flameout and left to steep: a whirlpool / hop stand.
            addition.use = calc::HopUse::Whirlpool;
            addition.time_min = *duration;
            addition.temperature_c = 80.0;
            warn(addition.hop.name + " steeps after flameout; assumed 80 °C");
        } else if (time) {
            addition.time_min = std::max(0.0, *time);
        }
    }
    return addition;
}

domain::YeastAddition RecipeReader::read_culture(const json& c, domain::Recipe& recipe) {
    domain::YeastAddition addition;
    domain::Yeast& yeast = addition.yeast;
    yeast.name = text(c, "name", "Yeast");
    yeast.laboratory = text(c, "producer");
    yeast.product_id = text(c, "product_id");

    const std::string type = text(c, "type", "ale");
    if (type == "lager") yeast.type = domain::YeastType::Lager;
    else if (type == "ale" || type == "wheat" || type == "kveik") yeast.type = domain::YeastType::Ale;
    else yeast.type = domain::YeastType::Other;
    if (yeast.type == domain::YeastType::Lager) recipe.pitch_rate = calc::kLagerPitchRate;

    const std::string form = text(c, "form", "liquid");
    if (form == "dry") yeast.form = domain::YeastForm::Dry;
    else if (form == "dregs") yeast.form = domain::YeastForm::Slurry;
    else yeast.form = domain::YeastForm::Liquid;

    if (const auto attenuation = fraction(c, "attenuation")) {
        yeast.attenuation = *attenuation;
    } else {
        warn(yeast.name + " has no attenuation; used 75 %");
    }

    if (is_count(c, "amount")) {
        addition.packs = measurement(c, "amount")->first;
        if (yeast.form == domain::YeastForm::Dry) {
            addition.mass_kg = 0.0115 * addition.packs;  // sachets
        }
    } else if (const auto kg = amount_kg(c)) {
        yeast.form = domain::YeastForm::Dry;
        addition.mass_kg = *kg;
    } else if (const auto l = amount_l(c)) {
        if (yeast.form == domain::YeastForm::Liquid) {
            addition.packs = 1.0;
            warn(yeast.name + " is measured by volume; counted as one pack");
        } else {
            addition.slurry_l = *l;
        }
    } else {
        warn_about_amount(c, yeast.name);
    }

    const auto cells = c.find("cell_count_billions");
    if (cells != c.end() && cells->is_number() && yeast.form == domain::YeastForm::Liquid && addition.packs > 0.0) {
        yeast.cells_per_pack_billion = cells->get<double>() / addition.packs;
    }
    return addition;
}

domain::MiscAddition RecipeReader::read_misc(const json& m) {
    domain::MiscAddition misc;
    misc.name = text(m, "name", "Addition");
    if (const auto amount = measurement(m, "amount")) {
        char formatted[64];
        std::snprintf(formatted, sizeof formatted, "%g %s", amount->first, amount->second.c_str());
        misc.amount = formatted;
    }
    if (const json* timing = object(m, "timing")) {
        const std::string use = text(*timing, "use", "add_to_boil");
        if (use == "add_to_mash") misc.use = domain::MiscUse::Mash;
        else if (use == "add_to_fermentation") misc.use = domain::MiscUse::Fermenter;
        else if (use == "add_to_package") misc.use = domain::MiscUse::Packaging;
        else misc.use = domain::MiscUse::Boil;
        if (const auto time = minutes(*timing, "time")) misc.time_min = *time;
    }
    return misc;
}

domain::MashStep RecipeReader::read_mash_step(const json& s) {
    domain::MashStep step;
    step.name = text(s, "name", "Step");
    if (const auto temperature = celsius(s, "step_temperature")) step.temperature_c = *temperature;
    if (const auto time = minutes(s, "step_time")) step.time_min = *time;
    return step;
}

} // namespace

BeerJsonImport import_beerjson(std::string_view text, const domain::Recipe& defaults) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("not valid JSON (") + e.what() + ")");
    }

    // Normally {"beerjson": {"recipes": [...]}}; accept a bare {"recipes": [...]} too.
    const json* document = &root;
    if (root.is_object() && root.contains("beerjson")) document = &root["beerjson"];
    if (!document->is_object() || !document->contains("recipes") || !(*document)["recipes"].is_array()) {
        throw std::runtime_error("no BeerJSON recipes found");
    }

    BeerJsonImport result;
    RecipeReader reader(result.warnings, defaults);
    for (const json& recipe : (*document)["recipes"]) {
        if (!recipe.is_object()) continue;
        try {
            result.recipes.push_back(reader.read(recipe));
        } catch (const json::exception& e) {
            result.warnings.push_back("skipped a recipe with an unexpected value (" + std::string(e.what()) + ")");
        }
    }
    return result;
}

} // namespace ninkasi::app
