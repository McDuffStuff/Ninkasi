#include "data/recipe_json.h"

#include "data/database.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace ninkasi::data {

namespace {

using nlohmann::json;
using namespace ninkasi::domain;

// Bumped if the layout ever changes in a way readers must handle.
constexpr int kFormatVersion = 1;

// --- Enum keys ----------------------------------------------------------------

template <typename E>
struct EnumKey {
    E value;
    std::string_view key;
};

constexpr EnumKey<FermentableType> kFermentableTypes[] = {
    {FermentableType::Grain, "grain"},     {FermentableType::Adjunct, "adjunct"},
    {FermentableType::Sugar, "sugar"},     {FermentableType::Extract, "extract"},
    {FermentableType::DryExtract, "dry_extract"}, {FermentableType::Fruit, "fruit"},
    {FermentableType::Other, "other"},
};
constexpr EnumKey<calc::MaltCategory> kMaltCategories[] = {
    {calc::MaltCategory::Base, "base"},       {calc::MaltCategory::Crystal, "crystal"},
    {calc::MaltCategory::Roasted, "roasted"}, {calc::MaltCategory::Acidulated, "acidulated"},
};
constexpr EnumKey<calc::HopForm> kHopForms[] = {
    {calc::HopForm::Pellet, "pellet"}, {calc::HopForm::Leaf, "leaf"},       {calc::HopForm::Plug, "plug"},
    {calc::HopForm::Powder, "powder"}, {calc::HopForm::Extract, "extract"},
};
constexpr EnumKey<calc::HopUse> kHopUses[] = {
    {calc::HopUse::Boil, "boil"},           {calc::HopUse::FirstWort, "first_wort"},
    {calc::HopUse::Whirlpool, "whirlpool"}, {calc::HopUse::Mash, "mash"},
    {calc::HopUse::DryHop, "dry_hop"},
};
constexpr EnumKey<YeastType> kYeastTypes[] = {
    {YeastType::Ale, "ale"}, {YeastType::Lager, "lager"}, {YeastType::Other, "other"},
};
constexpr EnumKey<YeastForm> kYeastForms[] = {
    {YeastForm::Liquid, "liquid"}, {YeastForm::Dry, "dry"}, {YeastForm::Slurry, "slurry"},
};
constexpr EnumKey<MiscUse> kMiscUses[] = {
    {MiscUse::Mash, "mash"},           {MiscUse::Boil, "boil"},           {MiscUse::Whirlpool, "whirlpool"},
    {MiscUse::Fermenter, "fermenter"}, {MiscUse::Packaging, "packaging"},
};
constexpr EnumKey<calc::StarterMethod> kStarterMethods[] = {
    {calc::StarterMethod::StirPlate, "stir_plate"},
    {calc::StarterMethod::Shaken, "shaken"},
    {calc::StarterMethod::NoAgitation, "no_agitation"},
};
constexpr EnumKey<calc::IbuFormula> kIbuFormulas[] = {
    {calc::IbuFormula::Tinseth, "tinseth"}, {calc::IbuFormula::Rager, "rager"}, {calc::IbuFormula::Garetz, "garetz"},
};
constexpr EnumKey<BoilGravity> kBoilGravities[] = {
    {BoilGravity::PreBoil, "pre_boil"}, {BoilGravity::Average, "average"}, {BoilGravity::PostBoil, "post_boil"},
};
constexpr EnumKey<calc::ColorModel> kColorModels[] = {
    {calc::ColorModel::Morey, "morey"}, {calc::ColorModel::Daniels, "daniels"}, {calc::ColorModel::Mosher, "mosher"},
};
constexpr EnumKey<calc::AbvFormula> kAbvFormulas[] = {
    {calc::AbvFormula::Simple, "simple"},
    {calc::AbvFormula::Alternate, "alternate"},
    {calc::AbvFormula::Balling, "balling"},
};

template <typename E, std::size_t N>
std::string key_of(E value, const EnumKey<E> (&table)[N]) {
    for (const EnumKey<E>& entry : table) {
        if (entry.value == value) return std::string(entry.key);
    }
    return std::string(table[0].key);
}

// The enum stored under `field`, or `fallback` if missing or unknown.
template <typename E, std::size_t N>
E enum_field(const json& j, const char* field, E fallback, const EnumKey<E> (&table)[N]) {
    const auto found = j.find(field);
    if (found == j.end() || !found->is_string()) return fallback;
    const std::string key = found->get<std::string>();
    for (const EnumKey<E>& entry : table) {
        if (entry.key == key) return entry.value;
    }
    return fallback;
}

// --- Field helpers ------------------------------------------------------------

template <typename T>
T field(const json& j, const char* name, T fallback) {
    const auto found = j.find(name);
    if (found == j.end() || found->is_null()) return fallback;
    return found->get<T>();
}

std::optional<double> optional_field(const json& j, const char* name) {
    const auto found = j.find(name);
    if (found == j.end() || found->is_null()) return std::nullopt;
    return found->get<double>();
}

json optional_value(const std::optional<double>& value) {
    return value ? json(*value) : json(nullptr);
}

// Calls `read` for each element of the array `name`, if present.
template <typename Read>
void each(const json& j, const char* name, Read read) {
    const auto found = j.find(name);
    if (found == j.end() || !found->is_array()) return;
    for (const json& element : *found) {
        if (element.is_object()) read(element);
    }
}

json parse(std::string_view text, const char* what) {
    try {
        json j = json::parse(text);
        if (!j.is_object()) throw DbError(std::string(what) + " is not a JSON object");
        return j;
    } catch (const json::exception& e) {
        throw DbError(std::string(what) + " is not valid JSON: " + e.what());
    }
}

// Wraps a conversion so type mismatches surface as DbError too.
template <typename T, typename Read>
T read_json(std::string_view text, const char* what, Read read) {
    const json j = parse(text, what);
    try {
        return read(j);
    } catch (const json::exception& e) {
        throw DbError(std::string(what) + " has an unexpected value: " + e.what());
    }
}

// --- Ingredients --------------------------------------------------------------

json fermentable_json(const Fermentable& f) {
    return {{"name", f.name},
            {"supplier", f.supplier},
            {"type", key_of(f.type, kFermentableTypes)},
            {"category", key_of(f.category, kMaltCategories)},
            {"yield", f.yield},
            {"color_lovibond", f.color_lovibond},
            {"di_ph", optional_value(f.di_ph)}};
}

Fermentable read_fermentable(const json& j) {
    const Fermentable d;
    return {field(j, "name", d.name),
            field(j, "supplier", d.supplier),
            enum_field(j, "type", d.type, kFermentableTypes),
            enum_field(j, "category", d.category, kMaltCategories),
            field(j, "yield", d.yield),
            field(j, "color_lovibond", d.color_lovibond),
            optional_field(j, "di_ph")};
}

json hop_json(const Hop& h) {
    return {{"name", h.name}, {"origin", h.origin}, {"form", key_of(h.form, kHopForms)}, {"alpha_acid", h.alpha_acid}};
}

Hop read_hop(const json& j) {
    const Hop d;
    return {field(j, "name", d.name), field(j, "origin", d.origin), enum_field(j, "form", d.form, kHopForms),
            field(j, "alpha_acid", d.alpha_acid)};
}

json yeast_json(const Yeast& y) {
    return {{"name", y.name},
            {"laboratory", y.laboratory},
            {"product_id", y.product_id},
            {"type", key_of(y.type, kYeastTypes)},
            {"form", key_of(y.form, kYeastForms)},
            {"attenuation", y.attenuation},
            {"cells_per_pack_billion", y.cells_per_pack_billion},
            {"cells_per_gram_billion", y.cells_per_gram_billion}};
}

Yeast read_yeast(const json& j) {
    const Yeast d;
    return {field(j, "name", d.name),
            field(j, "laboratory", d.laboratory),
            field(j, "product_id", d.product_id),
            enum_field(j, "type", d.type, kYeastTypes),
            enum_field(j, "form", d.form, kYeastForms),
            field(j, "attenuation", d.attenuation),
            field(j, "cells_per_pack_billion", d.cells_per_pack_billion),
            field(j, "cells_per_gram_billion", d.cells_per_gram_billion)};
}

// --- Recipe parts -------------------------------------------------------------

json equipment_json(const Equipment& e) {
    return {{"boil_off_l_per_hr", e.boil_off_l_per_hr},
            {"kettle_loss_l", e.kettle_loss_l},
            {"cooling_shrinkage", e.cooling_shrinkage},
            {"grain_absorption_l_per_kg", e.grain_absorption_l_per_kg},
            {"mash_tun_loss_l", e.mash_tun_loss_l},
            {"mash_thickness_l_per_kg", e.mash_thickness_l_per_kg},
            {"grain_displacement_l_per_kg", e.grain_displacement_l_per_kg}};
}

Equipment read_equipment(const json& j) {
    const Equipment d;
    return {field(j, "boil_off_l_per_hr", d.boil_off_l_per_hr),
            field(j, "kettle_loss_l", d.kettle_loss_l),
            field(j, "cooling_shrinkage", d.cooling_shrinkage),
            field(j, "grain_absorption_l_per_kg", d.grain_absorption_l_per_kg),
            field(j, "mash_tun_loss_l", d.mash_tun_loss_l),
            field(j, "mash_thickness_l_per_kg", d.mash_thickness_l_per_kg),
            field(j, "grain_displacement_l_per_kg", d.grain_displacement_l_per_kg)};
}

json water_profile_json(const calc::WaterProfile& w) {
    return {{"calcium", w.calcium},   {"magnesium", w.magnesium}, {"sodium", w.sodium},
            {"chloride", w.chloride}, {"sulfate", w.sulfate},     {"bicarbonate", w.bicarbonate}};
}

calc::WaterProfile read_water_profile(const json& j) {
    return {field(j, "calcium", 0.0),  field(j, "magnesium", 0.0), field(j, "sodium", 0.0),
            field(j, "chloride", 0.0), field(j, "sulfate", 0.0),   field(j, "bicarbonate", 0.0)};
}

json water_json(const Water& w) {
    json salts = json::array();
    for (const calc::SaltAddition& salt : w.salts) {
        salts.push_back({{"salt", std::string(calc::info(salt.salt).key)}, {"mass_kg", salt.mass_kg}});
    }
    json acids = json::array();
    for (const calc::AcidAddition& acid : w.mash_acids) {
        acids.push_back({{"acid", std::string(calc::info(acid.acid).key)}, {"volume_l", acid.volume_l}});
    }
    return {{"source", water_profile_json(w.source)},
            {"salts", salts},
            {"mash_acids", acids},
            {"target_mash_ph", w.target_mash_ph}};
}

Water read_water(const json& j) {
    Water w;
    if (const auto source = j.find("source"); source != j.end() && source->is_object()) {
        w.source = read_water_profile(*source);
    }
    each(j, "salts", [&](const json& s) {
        if (const auto salt = calc::salt_from_key(field(s, "salt", std::string()))) {
            w.salts.push_back({*salt, field(s, "mass_kg", 0.0)});
        }
    });
    each(j, "mash_acids", [&](const json& a) {
        if (const auto acid = calc::acid_from_key(field(a, "acid", std::string()))) {
            w.mash_acids.push_back({*acid, field(a, "volume_l", 0.0)});
        }
    });
    w.target_mash_ph = field(j, "target_mash_ph", w.target_mash_ph);
    return w;
}

json recipe_json(const Recipe& r) {
    json fermentables = json::array();
    for (const FermentableAddition& a : r.fermentables) {
        fermentables.push_back(
            {{"library_id", a.library_id}, {"fermentable", fermentable_json(a.fermentable)}, {"mass_kg", a.mass_kg}});
    }
    json hops = json::array();
    for (const HopAddition& a : r.hops) {
        hops.push_back({{"library_id", a.library_id},
                        {"hop", hop_json(a.hop)},
                        {"mass_kg", a.mass_kg},
                        {"use", key_of(a.use, kHopUses)},
                        {"time_min", a.time_min},
                        {"temperature_c", a.temperature_c}});
    }
    json yeasts = json::array();
    for (const YeastAddition& a : r.yeasts) {
        yeasts.push_back({{"library_id", a.library_id},
                          {"yeast", yeast_json(a.yeast)},
                          {"packs", a.packs},
                          {"mass_kg", a.mass_kg},
                          {"slurry_l", a.slurry_l},
                          {"slurry_cells_per_ml_billion", a.slurry_cells_per_ml_billion},
                          {"viability", a.viability}});
    }
    json miscs = json::array();
    for (const MiscAddition& a : r.miscs) {
        miscs.push_back(
            {{"name", a.name}, {"amount", a.amount}, {"use", key_of(a.use, kMiscUses)}, {"time_min", a.time_min}});
    }
    json mash = json::array();
    for (const MashStep& step : r.mash_steps) {
        mash.push_back({{"name", step.name}, {"temperature_c", step.temperature_c}, {"time_min", step.time_min}});
    }

    return {{"format", kFormatVersion},
            {"name", r.name},
            {"style", r.style},
            {"author", r.author},
            {"notes", r.notes},
            {"batch_volume_l", r.batch_volume_l},
            {"boil_time_min", r.boil_time_min},
            {"efficiency", r.efficiency},
            {"pitch_rate", r.pitch_rate},
            {"equipment", equipment_json(r.equipment)},
            {"fermentables", fermentables},
            {"hops", hops},
            {"yeasts", yeasts},
            {"miscs", miscs},
            {"mash_steps", mash},
            {"water", water_json(r.water)},
            {"starter",
             {{"enabled", r.starter.enabled},
              {"volume_l", r.starter.volume_l},
              {"gravity_sg", r.starter.gravity_sg},
              {"method", key_of(r.starter.method, kStarterMethods)}}}};
}

const json& object_or_empty(const json& j, const char* name) {
    static const json kEmpty = json::object();
    const auto found = j.find(name);
    return (found != j.end() && found->is_object()) ? *found : kEmpty;
}

Recipe read_recipe(const json& j) {
    Recipe r;
    r.name = field(j, "name", r.name);
    r.style = field(j, "style", r.style);
    r.author = field(j, "author", r.author);
    r.notes = field(j, "notes", r.notes);
    r.batch_volume_l = field(j, "batch_volume_l", r.batch_volume_l);
    r.boil_time_min = field(j, "boil_time_min", r.boil_time_min);
    r.efficiency = field(j, "efficiency", r.efficiency);
    r.pitch_rate = field(j, "pitch_rate", r.pitch_rate);
    r.equipment = read_equipment(object_or_empty(j, "equipment"));

    each(j, "fermentables", [&](const json& a) {
        r.fermentables.push_back({field<std::int64_t>(a, "library_id", 0),
                                  read_fermentable(object_or_empty(a, "fermentable")), field(a, "mass_kg", 0.0)});
    });
    each(j, "hops", [&](const json& a) {
        HopAddition h;
        h.library_id = field<std::int64_t>(a, "library_id", 0);
        h.hop = read_hop(object_or_empty(a, "hop"));
        h.mass_kg = field(a, "mass_kg", h.mass_kg);
        h.use = enum_field(a, "use", h.use, kHopUses);
        h.time_min = field(a, "time_min", h.time_min);
        h.temperature_c = field(a, "temperature_c", h.temperature_c);
        r.hops.push_back(std::move(h));
    });
    each(j, "yeasts", [&](const json& a) {
        YeastAddition y;
        y.library_id = field<std::int64_t>(a, "library_id", 0);
        y.yeast = read_yeast(object_or_empty(a, "yeast"));
        y.packs = field(a, "packs", y.packs);
        y.mass_kg = field(a, "mass_kg", y.mass_kg);
        y.slurry_l = field(a, "slurry_l", y.slurry_l);
        y.slurry_cells_per_ml_billion = field(a, "slurry_cells_per_ml_billion", y.slurry_cells_per_ml_billion);
        y.viability = field(a, "viability", y.viability);
        r.yeasts.push_back(std::move(y));
    });
    each(j, "miscs", [&](const json& a) {
        MiscAddition m;
        m.name = field(a, "name", m.name);
        m.amount = field(a, "amount", m.amount);
        m.use = enum_field(a, "use", m.use, kMiscUses);
        m.time_min = field(a, "time_min", m.time_min);
        r.miscs.push_back(std::move(m));
    });
    each(j, "mash_steps", [&](const json& s) {
        MashStep step;
        step.name = field(s, "name", step.name);
        step.temperature_c = field(s, "temperature_c", step.temperature_c);
        step.time_min = field(s, "time_min", step.time_min);
        r.mash_steps.push_back(std::move(step));
    });
    r.water = read_water(object_or_empty(j, "water"));

    const json& starter = object_or_empty(j, "starter");
    r.starter.enabled = field(starter, "enabled", r.starter.enabled);
    r.starter.volume_l = field(starter, "volume_l", r.starter.volume_l);
    r.starter.gravity_sg = field(starter, "gravity_sg", r.starter.gravity_sg);
    r.starter.method = enum_field(starter, "method", r.starter.method, kStarterMethods);
    return r;
}

// --- Calculation settings -----------------------------------------------------

json calc_settings_json(const CalcSettings& s) {
    json form_factors = json::object();
    for (std::size_t i = 0; i < calc::kHopFormCount; ++i) {
        form_factors[key_of(static_cast<calc::HopForm>(i), kHopForms)] = s.ibu.form_factor[i];
    }
    return {{"ibu_formula", key_of(s.ibu.formula, kIbuFormulas)},
            {"utilization_scale", s.ibu.utilization_scale},
            {"form_factors", form_factors},
            {"first_wort_factor", s.ibu.first_wort_factor},
            {"mash_hop_factor", s.ibu.mash_hop_factor},
            {"elevation_m", s.ibu.elevation_m},
            {"boil_gravity", key_of(s.boil_gravity, kBoilGravities)},
            {"color_model", key_of(s.color_model, kColorModels)},
            {"abv_formula", key_of(s.abv_formula, kAbvFormulas)}};
}

CalcSettings read_calc_settings(const json& j) {
    CalcSettings s;
    s.ibu.formula = enum_field(j, "ibu_formula", s.ibu.formula, kIbuFormulas);
    s.ibu.utilization_scale = field(j, "utilization_scale", s.ibu.utilization_scale);
    const json& form_factors = object_or_empty(j, "form_factors");
    for (std::size_t i = 0; i < calc::kHopFormCount; ++i) {
        const std::string key = key_of(static_cast<calc::HopForm>(i), kHopForms);
        s.ibu.form_factor[i] = field(form_factors, key.c_str(), s.ibu.form_factor[i]);
    }
    s.ibu.first_wort_factor = field(j, "first_wort_factor", s.ibu.first_wort_factor);
    s.ibu.mash_hop_factor = field(j, "mash_hop_factor", s.ibu.mash_hop_factor);
    s.ibu.elevation_m = field(j, "elevation_m", s.ibu.elevation_m);
    s.boil_gravity = enum_field(j, "boil_gravity", s.boil_gravity, kBoilGravities);
    s.color_model = enum_field(j, "color_model", s.color_model, kColorModels);
    s.abv_formula = enum_field(j, "abv_formula", s.abv_formula, kAbvFormulas);
    return s;
}

} // namespace

std::string to_json(const Recipe& recipe) { return recipe_json(recipe).dump(); }
Recipe recipe_from_json(std::string_view text) { return read_json<Recipe>(text, "recipe", read_recipe); }

std::string to_json(const Fermentable& fermentable) { return fermentable_json(fermentable).dump(); }
Fermentable fermentable_from_json(std::string_view text) {
    return read_json<Fermentable>(text, "fermentable", read_fermentable);
}

std::string to_json(const Hop& hop) { return hop_json(hop).dump(); }
Hop hop_from_json(std::string_view text) { return read_json<Hop>(text, "hop", read_hop); }

std::string to_json(const Yeast& yeast) { return yeast_json(yeast).dump(); }
Yeast yeast_from_json(std::string_view text) { return read_json<Yeast>(text, "yeast", read_yeast); }

std::string to_json(const CalcSettings& settings) { return calc_settings_json(settings).dump(); }
CalcSettings calc_settings_from_json(std::string_view text) {
    return read_json<CalcSettings>(text, "calculation settings", read_calc_settings);
}

} // namespace ninkasi::data
