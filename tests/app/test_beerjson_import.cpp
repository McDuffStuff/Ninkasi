#include "app/beerjson_import.h"
#include "calc/extract.h"
#include "calc/units.h"
#include "test_helpers.h"

#include <algorithm>
#include <stdexcept>
#include <string>

using namespace ninkasi;

namespace {

// Written to the BeerJSON 1.0 schema, exercising unit conversions and the
// different kinds of additions.
constexpr const char* kIpa = R"json({
  "beerjson": {
    "version": 1.0,
    "recipes": [{
      "name": "Imported IPA",
      "type": "all grain",
      "author": "Someone Else",
      "batch_size": {"value": 5.5, "unit": "gal"},
      "efficiency": {"brewhouse": {"value": 70, "unit": "%"}},
      "style": {"name": "American IPA", "category": "IPA", "style_guide": "BJCP 2021", "type": "beer"},
      "boil": {"boil_time": {"value": 75, "unit": "min"}},
      "ingredients": {
        "fermentable_additions": [
          {"name": "Pale Malt", "type": "grain", "grain_group": "base",
           "yield": {"potential": {"value": 1.037, "unit": "sg"}},
           "color": {"value": 3.5, "unit": "EBC"}, "amount": {"value": 11, "unit": "lb"}},
          {"name": "Caramel 40", "type": "grain", "grain_group": "caramel",
           "yield": {"fine_grind": {"value": 74, "unit": "%"}},
           "color": {"value": 40, "unit": "Lovi"}, "amount": {"value": 500, "unit": "g"}},
          {"name": "Acidulated Malt", "type": "grain",
           "yield": {"fine_grind": {"value": 58, "unit": "%"}},
           "color": {"value": 2, "unit": "SRM"}, "amount": {"value": 0.1, "unit": "kg"}},
          {"name": "Flaked Oats", "type": "grain", "grain_group": "flaked",
           "yield": {"fine_grind": {"value": 70, "unit": "%"}},
           "color": {"value": 1, "unit": "SRM"}, "amount": {"value": 1, "unit": "lb"}},
          {"name": "Honey", "type": "honey",
           "yield": {"potential": {"value": 1.035, "unit": "sg"}},
           "color": {"value": 2, "unit": "SRM"}, "amount": {"value": 0.5, "unit": "l"}}
        ],
        "hop_additions": [
          {"name": "Magnum", "form": "pellet", "alpha_acid": {"value": 13, "unit": "%"},
           "amount": {"value": 1, "unit": "oz"},
           "timing": {"use": "add_to_boil", "time": {"value": 60, "unit": "min"}}},
          {"name": "Citra", "form": "leaf (wet)", "alpha_acid": {"value": 12, "unit": "%"},
           "amount": {"value": 2, "unit": "oz"},
           "timing": {"use": "add_to_boil", "time": {"value": 0, "unit": "min"},
                      "duration": {"value": 20, "unit": "min"}}},
          {"name": "Mosaic", "form": "pellet", "alpha_acid": {"value": 12.5, "unit": "%"},
           "amount": {"value": 3, "unit": "oz"},
           "timing": {"use": "add_to_fermentation", "duration": {"value": 4, "unit": "day"}}}
        ],
        "culture_additions": [
          {"name": "California Ale", "type": "ale", "form": "liquid", "producer": "A Lab",
           "product_id": "001", "attenuation": {"value": 77, "unit": "%"},
           "amount": {"value": 2, "unit": "pkg"}}
        ],
        "miscellaneous_additions": [
          {"name": "Whirlfloc", "type": "fining", "amount": {"value": 1, "unit": "each"},
           "timing": {"use": "add_to_boil", "time": {"value": 15, "unit": "min"}}}
        ]
      },
      "mash": {"name": "Single infusion", "grain_temperature": {"value": 20, "unit": "C"},
               "mash_steps": [{"name": "Mash in", "type": "infusion",
                               "step_temperature": {"value": 152, "unit": "F"},
                               "step_time": {"value": 1, "unit": "hr"}}]},
      "notes": "Tasty"
    }]
  }
})json";

domain::Recipe brewery_defaults() {
    domain::Recipe defaults;
    defaults.batch_volume_l = 1000.0;
    defaults.equipment.boil_off_l_per_hr = 33.0;
    defaults.water.source.calcium = 50.0;
    defaults.mash_steps = {{"House step", 65.0, 45.0}};
    return defaults;
}

bool mentions(const std::vector<std::string>& warnings, const std::string& text) {
    return std::any_of(warnings.begin(), warnings.end(),
                       [&](const std::string& w) { return w.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("BeerJSON recipe fields") {
    const app::BeerJsonImport result = app::import_beerjson(kIpa, brewery_defaults());
    REQUIRE(result.recipes.size() == 1);
    const domain::Recipe& r = result.recipes[0];
    CHECK(r.name == "Imported IPA");
    CHECK(r.author == "Someone Else");
    CHECK(r.style == "American IPA");
    CHECK(r.notes == "Tasty");
    CHECK(r.batch_volume_l == near(5.5 * calc::kLitersPerUsGallon, 1e-9));
    CHECK(r.efficiency == near(0.70, 1e-12));
    CHECK(r.boil_time_min == near(75.0, 1e-12));

    // What BeerJSON doesn't carry comes from the brewery's defaults.
    CHECK(r.equipment.boil_off_l_per_hr == 33.0);
    CHECK(r.water.source.calcium == 50.0);

    REQUIRE(r.mash_steps.size() == 1);  // the file's mash replaces the default one
    CHECK(r.mash_steps[0].name == "Mash in");
    CHECK(r.mash_steps[0].temperature_c == near(calc::to_canonical(152.0, calc::Unit::Fahrenheit), 1e-9));
    CHECK(r.mash_steps[0].time_min == near(60.0, 1e-12));
}

TEST_CASE("BeerJSON fermentables") {
    const app::BeerJsonImport result = app::import_beerjson(kIpa, brewery_defaults());
    const auto& f = result.recipes.at(0).fermentables;
    REQUIRE(f.size() == 5);

    CHECK(f[0].mass_kg == near(11.0 * calc::kKilogramsPerPound, 1e-9));
    CHECK(f[0].fermentable.yield == near(calc::yield_from_potential_sg(1.037), 1e-12));
    CHECK(f[0].fermentable.color_lovibond == near(3.5 / 1.97, 1e-9));  // EBC -> °L
    CHECK(f[0].fermentable.category == calc::MaltCategory::Base);

    CHECK(f[1].fermentable.category == calc::MaltCategory::Crystal);  // "caramel"
    CHECK(f[1].fermentable.yield == near(0.74, 1e-12));
    CHECK(f[1].mass_kg == near(0.5, 1e-12));

    CHECK(f[2].fermentable.category == calc::MaltCategory::Acidulated);
    CHECK(f[3].fermentable.type == domain::FermentableType::Adjunct);  // "flaked"

    CHECK(f[4].fermentable.type == domain::FermentableType::Sugar);  // honey
    CHECK(f[4].mass_kg == near(0.5 * 1.42, 1e-9));
    CHECK(mentions(result.warnings, "Honey is measured by volume"));
}

TEST_CASE("BeerJSON hops, yeast and other additions") {
    const app::BeerJsonImport result = app::import_beerjson(kIpa, brewery_defaults());
    const domain::Recipe& r = result.recipes.at(0);

    REQUIRE(r.hops.size() == 3);
    CHECK(r.hops[0].use == calc::HopUse::Boil);
    CHECK(r.hops[0].time_min == near(60.0, 1e-12));
    CHECK(r.hops[0].mass_kg == near(calc::kKilogramsPerPound / 16.0, 1e-12));
    CHECK(r.hops[0].hop.alpha_acid == near(0.13, 1e-12));
    CHECK(r.hops[1].use == calc::HopUse::Whirlpool);  // flameout with a steep
    CHECK(r.hops[1].time_min == near(20.0, 1e-12));
    CHECK(r.hops[1].hop.form == calc::HopForm::Leaf);
    CHECK(mentions(result.warnings, "assumed 80"));
    CHECK(r.hops[2].use == calc::HopUse::DryHop);

    REQUIRE(r.yeasts.size() == 1);
    const domain::YeastAddition& yeast = r.yeasts[0];
    CHECK(yeast.yeast.laboratory == "A Lab");
    CHECK(yeast.yeast.product_id == "001");
    CHECK(yeast.yeast.form == domain::YeastForm::Liquid);
    CHECK(yeast.yeast.attenuation == near(0.77, 1e-12));
    CHECK(yeast.packs == 2.0);

    REQUIRE(r.miscs.size() == 1);
    CHECK(r.miscs[0].amount == "1 each");
    CHECK(r.miscs[0].use == domain::MiscUse::Boil);
    CHECK(r.miscs[0].time_min == near(15.0, 1e-12));
}

TEST_CASE("BeerJSON lager yeast raises the pitch rate") {
    const char* lager = R"({"recipes": [{"name": "Helles", "batch_size": {"value": 20, "unit": "l"},
        "ingredients": {"fermentable_additions": [],
          "culture_additions": [{"name": "Lager", "type": "lager", "form": "dry",
                                 "amount": {"value": 23, "unit": "g"}}]}}]})";
    const app::BeerJsonImport result = app::import_beerjson(lager, brewery_defaults());  // bare "recipes" accepted
    REQUIRE(result.recipes.size() == 1);
    CHECK(result.recipes[0].pitch_rate == 1.5);
    CHECK(result.recipes[0].yeasts.at(0).mass_kg == near(0.023, 1e-12));
    CHECK(mentions(result.warnings, "no attenuation"));
}

TEST_CASE("BeerJSON problems are reported") {
    CHECK_THROWS_AS(app::import_beerjson("{not json", {}), std::runtime_error);
    CHECK_THROWS_AS(app::import_beerjson(R"({"beerjson": {"version": 1}})", {}), std::runtime_error);

    const char* odd = R"({"recipes": [{"name": "Odd",
        "batch_size": {"value": 3, "unit": "hogshead"},
        "ingredients": {"fermentable_additions": [
          {"name": "Mystery", "type": "grain", "color": {"value": 2, "unit": "SRM"},
           "amount": {"value": 2, "unit": "stone"}}]}}]})";
    const app::BeerJsonImport result = app::import_beerjson(odd, brewery_defaults());
    REQUIRE(result.recipes.size() == 1);
    CHECK(result.recipes[0].batch_volume_l == 1000.0);  // kept the default
    CHECK(mentions(result.warnings, "unknown unit \"hogshead\""));
    CHECK(mentions(result.warnings, "Mystery has an amount in an unknown unit (\"stone\")"));
    CHECK(mentions(result.warnings, "Mystery has no yield"));
}
