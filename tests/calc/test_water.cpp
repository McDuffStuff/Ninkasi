#include "calc/units.h"
#include "calc/water.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

namespace {

WaterProfile one_gram_per_gallon(Salt salt) {
    const SaltAddition addition{salt, 0.001};
    return add_salts(WaterProfile{}, std::span(&addition, 1), kLitersPerUsGallon);
}

} // namespace

TEST_CASE("salts add ions by molar mass") {
    // Palmer, "How to Brew": ppm added by 1 g of each salt per US gallon.
    const WaterProfile gypsum = one_gram_per_gallon(Salt::Gypsum);
    CHECK(gypsum.calcium == near(61.5, 0.1));
    CHECK(gypsum.sulfate == near(147.4, 0.1));

    const WaterProfile calcium_chloride = one_gram_per_gallon(Salt::CalciumChloride);
    CHECK(calcium_chloride.calcium == near(72.0, 0.1));
    CHECK(calcium_chloride.chloride == near(127.4, 0.1));

    const WaterProfile epsom = one_gram_per_gallon(Salt::EpsomSalt);
    CHECK(epsom.magnesium == near(26.1, 0.1));
    CHECK(epsom.sulfate == near(103.0, 0.1));

    const WaterProfile salt = one_gram_per_gallon(Salt::TableSalt);
    CHECK(salt.sodium == near(103.9, 0.1));
    CHECK(salt.chloride == near(160.3, 0.1));

    const WaterProfile soda = one_gram_per_gallon(Salt::BakingSoda);
    CHECK(soda.sodium == near(72.3, 0.1));
    CHECK(soda.bicarbonate == near(191.9, 0.1));

    const WaterProfile chalk = one_gram_per_gallon(Salt::Chalk);
    CHECK(chalk.calcium == near(105.8, 0.1));

    CHECK(std::isnan(add_salts(WaterProfile{}, {}, 0.0).calcium));
}

TEST_CASE("salt and acid keys round-trip") {
    for (const SaltInfo& salt : salts()) CHECK(salt_from_key(salt.key) == salt.salt);
    for (const AcidInfo& acid : acids()) CHECK(acid_from_key(acid.key) == acid.acid);
    CHECK_FALSE(salt_from_key("pixie_dust").has_value());
}

TEST_CASE("alkalinity, residual alkalinity and sulfate:chloride") {
    const WaterProfile water{.calcium = 50, .magnesium = 10, .chloride = 50, .sulfate = 100, .bicarbonate = 200};
    CHECK(alkalinity(water) == near(164.0, 0.1));
    // Palmer's form: RA = alkalinity − Ca/1.4 − Mg/1.7.
    CHECK(residual_alkalinity(water) == near(164.0 - 50.0 / 1.4 - 10.0 / 1.7, 0.2));
    CHECK(sulfate_chloride_ratio(water) == near(2.0, 1e-12));
    CHECK(std::isnan(sulfate_chloride_ratio(WaterProfile{})));
}

TEST_CASE("blending waters averages by volume") {
    const WaterProfile tap{.calcium = 80, .bicarbonate = 240};
    const WaterProfile mixed = blend(tap, 10.0, WaterProfile{}, 30.0);  // one part tap, three parts RO
    CHECK(mixed.calcium == near(20.0, 1e-12));
    CHECK(mixed.bicarbonate == near(60.0, 1e-12));
}

TEST_CASE("acid strength") {
    // 88 % lactic acid carries 11.8 mEq/mL; at mash pH ~97 % of it acts.
    CHECK(acid_meq(Acid::Lactic88, 0.001, 9.0) == near(11.81, 0.01));
    CHECK(acid_meq(Acid::Lactic88, 0.001, 5.4) == near(11.48, 0.01));
    // 10 % phosphoric: the first proton fully, a little of the second.
    CHECK(acid_meq(Acid::Phosphoric10, 0.001, 5.4) == near(1.091, 0.002));
    CHECK(acid_volume_l(Acid::Lactic88, acid_meq(Acid::Lactic88, 0.002), 5.4) == near(0.002, 1e-12));
}

TEST_CASE("mash pH estimate") {
    const MashGrain base{.mass_kg = 5.0, .category = MaltCategory::Base, .color_lovibond = 2.0};
    const MashGrain grist[] = {base};

    // Base malt in distilled water sits at its own distilled-water pH.
    CHECK(estimate_mash_ph(grist, WaterProfile{}, 15.0) == near(5.7, 1e-9));

    // Alkaline water pushes the pH up; acid brings it down.
    const WaterProfile hard{.calcium = 40, .bicarbonate = 250};
    const double with_hard_water = estimate_mash_ph(grist, hard, 15.0);
    CHECK(with_hard_water > 5.8);
    const AcidAddition two_ml{Acid::Lactic88, 0.002};
    CHECK(estimate_mash_ph(grist, hard, 15.0, std::span(&two_ml, 1)) < with_hard_water);

    // Acid needed to hit a target really does hit it.
    const double needed = acid_needed_meq(grist, hard, 15.0, {}, 5.4);
    REQUIRE(needed > 0.0);
    const AcidAddition dose{Acid::Lactic88, acid_volume_l(Acid::Lactic88, needed, 5.4)};
    CHECK(estimate_mash_ph(grist, hard, 15.0, std::span(&dose, 1)) == near(5.4, 1e-6));

    // Specialty malts lower the pH.
    const MashGrain with_crystal[] = {base, {.mass_kg = 0.5, .category = MaltCategory::Crystal, .color_lovibond = 60.0}};
    CHECK(estimate_mash_ph(with_crystal, WaterProfile{}, 15.0) < 5.7);

    CHECK(std::isnan(estimate_mash_ph({}, WaterProfile{}, 15.0)));
}

TEST_CASE("acidulated malt lowers the pH about 0.1 per percent of the grist") {
    const MashGrain grist[] = {
        {.mass_kg = 4.9, .category = MaltCategory::Base},
        {.mass_kg = 0.1, .category = MaltCategory::Acidulated},  // 2 % of the grist
    };
    const double drop = 5.7 - estimate_mash_ph(grist, WaterProfile{}, 15.0);
    CHECK(drop > 0.1);
    CHECK(drop < 0.3);
}

TEST_CASE("default distilled-water pH by malt type") {
    CHECK(default_di_ph(MaltCategory::Base, 2.0) == 5.7);
    CHECK(default_di_ph(MaltCategory::Crystal, 10.0) == near(5.38, 1e-12));
    CHECK(default_di_ph(MaltCategory::Crystal, 60.0) == near(4.66, 1e-12));
    CHECK(default_di_ph(MaltCategory::Crystal, 150.0) == 4.5);  // floor
    CHECK(default_di_ph(MaltCategory::Roasted, 500.0) == 4.7);
}
