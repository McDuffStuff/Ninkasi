#pragma once
// Recipes and the ingredients they are made from.
//
// An ingredient's specification (a malt's yield and color, a hop's alpha
// acids...) is copied into each recipe that uses it, so later edits to the
// library never change an existing recipe. `library_id` remembers where it
// came from.

#include "calc/bitterness.h"
#include "calc/water.h"
#include "calc/yeast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

// --- Ingredients ------------------------------------------------------------

enum class FermentableType { Grain, Adjunct, Sugar, Extract, DryExtract, Fruit, Other };

// Grains and adjuncts are mashed; everything else dissolves in the kettle.
bool is_mashed(FermentableType type);

struct Fermentable {
    std::string name;
    std::string supplier;
    FermentableType type = FermentableType::Grain;
    calc::MaltCategory category = calc::MaltCategory::Base;  // for the mash pH estimate
    double yield = 0.80;           // as-is extract yield, fraction
    double color_lovibond = 2.0;
    std::optional<double> di_ph;   // measured distilled-water mash pH, if known

    bool operator==(const Fermentable&) const = default;
};

struct Hop {
    std::string name;
    std::string origin;
    calc::HopForm form = calc::HopForm::Pellet;
    double alpha_acid = 0.05;  // fraction

    bool operator==(const Hop&) const = default;
};

enum class YeastType { Ale, Lager, Other };
enum class YeastForm { Liquid, Dry, Slurry };

struct Yeast {
    std::string name;
    std::string laboratory;
    std::string product_id;
    YeastType type = YeastType::Ale;
    YeastForm form = YeastForm::Liquid;
    double attenuation = 0.75;            // apparent, fraction
    double cells_per_pack_billion = 100;  // liquid packs
    double cells_per_gram_billion = 10;   // dry yeast; makers guarantee at least ~6

    bool operator==(const Yeast&) const = default;
};

// A library entry: an ingredient and its database id.
template <typename T>
struct LibraryItem {
    std::int64_t id = 0;
    T item;
};

// --- Recipe parts -----------------------------------------------------------

struct FermentableAddition {
    std::int64_t library_id = 0;
    Fermentable fermentable;
    double mass_kg = 0.0;

    bool operator==(const FermentableAddition&) const = default;
};

struct HopAddition {
    std::int64_t library_id = 0;
    Hop hop;
    double mass_kg = 0.0;
    calc::HopUse use = calc::HopUse::Boil;
    double time_min = 60.0;        // Boil: before flameout. Whirlpool: steep time.
    double temperature_c = 100.0;  // Whirlpool only

    bool operator==(const HopAddition&) const = default;
};

struct YeastAddition {
    std::int64_t library_id = 0;
    Yeast yeast;
    double packs = 1.0;            // liquid
    double mass_kg = 0.0115;       // dry: one 11.5 g sachet
    double slurry_l = 0.0;         // slurry
    double slurry_cells_per_ml_billion = 1.0;
    double viability = 1.0;        // fraction of cells alive at pitching

    bool operator==(const YeastAddition&) const = default;
};

enum class MiscUse { Mash, Boil, Whirlpool, Fermenter, Packaging };

struct MiscAddition {
    std::string name;
    std::string amount;  // free text: "1 tablet", "5 g", "2 tsp"
    MiscUse use = MiscUse::Boil;
    double time_min = 0.0;

    bool operator==(const MiscAddition&) const = default;
};

struct MashStep {
    std::string name;
    double temperature_c = 67.0;
    double time_min = 60.0;

    bool operator==(const MashStep&) const = default;
};

// The brewhouse figures that turn a batch size into brew-day volumes.
struct Equipment {
    double boil_off_l_per_hr = 0.0;
    double kettle_loss_l = 0.0;
    double cooling_shrinkage = 0.04;
    double grain_absorption_l_per_kg = 1.0;
    double mash_tun_loss_l = 0.0;
    double mash_thickness_l_per_kg = 3.0;
    double grain_displacement_l_per_kg = 0.67;

    bool operator==(const Equipment&) const = default;
};

// A named water: the brewery's water report, a classic brewing water, or
// one from another brewer's BeerJSON. Kept in the ingredient library.
struct WaterSource {
    std::string name;
    calc::WaterProfile ions;  // ppm
    std::string notes;

    bool operator==(const WaterSource&) const = default;
};

struct Water {
    calc::WaterProfile source;
    std::string source_name;  // the water profile it came from; empty when typed in
    std::vector<calc::SaltAddition> salts;       // dissolved in all of the brewing water
    std::vector<calc::AcidAddition> mash_acids;  // added to the mash
    double target_mash_ph = 5.4;

    bool operator==(const Water&) const = default;
};

struct Starter {
    bool enabled = false;
    double volume_l = 2.0;
    double gravity_sg = 1.036;
    calc::StarterMethod method = calc::StarterMethod::StirPlate;

    bool operator==(const Starter&) const = default;
};

// --- Recipe -----------------------------------------------------------------

struct Recipe {
    std::int64_t id = 0;
    std::string name;
    std::string style;
    std::string author;
    std::string notes;

    double batch_volume_l = 0.0;  // into the fermenter
    double boil_time_min = 60.0;
    // Brewhouse efficiency: extract reaching the fermenter from mashed
    // fermentables, as a fraction of their potential (BeerJSON's definition).
    double efficiency = 0.72;
    double pitch_rate = calc::kAlePitchRate;  // million cells/mL/°P

    Equipment equipment;
    std::vector<FermentableAddition> fermentables;
    std::vector<HopAddition> hops;
    std::vector<YeastAddition> yeasts;
    std::vector<MiscAddition> miscs;
    std::vector<MashStep> mash_steps;
    Water water;
    Starter starter;

    bool operator==(const Recipe&) const = default;
};

// --- Names shown to the brewer ----------------------------------------------

std::string_view name(FermentableType type);
std::string_view name(calc::MaltCategory category);
std::string_view name(calc::HopForm form);
std::string_view name(calc::HopUse use);
std::string_view name(YeastType type);
std::string_view name(YeastForm form);
std::string_view name(MiscUse use);
std::string_view name(calc::StarterMethod method);

} // namespace ninkasi::domain
