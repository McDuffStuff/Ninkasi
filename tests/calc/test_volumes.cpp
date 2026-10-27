#include "calc/volumes.h"
#include "test_helpers.h"

#include <cmath>

using namespace ninkasi::calc;

namespace {

BrewVolumeInputs twenty_liter_batch() {
    BrewVolumeInputs in;
    in.fermenter_volume_l = 20.0;
    in.kettle_loss_l = 2.0;
    in.cooling_shrinkage = 0.04;
    in.boil_off_l_per_hr = 4.0;
    in.boil_time_min = 60.0;
    in.grain_mass_kg = 5.0;
    in.grain_absorption_l_per_kg = 1.0;
    in.mash_tun_loss_l = 1.0;
    in.mash_thickness_l_per_kg = 3.0;
    in.grain_displacement_l_per_kg = 0.67;
    return in;
}

} // namespace

TEST_CASE("brew volumes work back from the fermenter") {
    const BrewVolumes v = plan_brew_volumes(twenty_liter_batch());

    const double post_boil = 22.0 / 0.96;
    CHECK(v.fermenter_volume_l == near(20.0, 1e-12));
    CHECK(v.knockout_volume_l == near(22.0, 1e-12));
    CHECK(v.post_boil_volume_l == near(post_boil, 1e-12));
    CHECK(v.pre_boil_volume_l == near(post_boil + 4.0, 1e-12));
    CHECK(v.total_water_l == near(post_boil + 4.0 + 5.0 + 1.0, 1e-12));
    CHECK(v.strike_water_l == near(15.0, 1e-12));
    CHECK(v.sparge_water_l == near(v.total_water_l - 15.0, 1e-12));
    CHECK(v.mash_volume_l == near(15.0 + 3.35, 1e-12));
}

TEST_CASE("a thin enough mash uses all the water and needs no sparge") {
    BrewVolumeInputs in = twenty_liter_batch();
    in.mash_thickness_l_per_kg = 10.0;
    const BrewVolumes v = plan_brew_volumes(in);
    CHECK(v.strike_water_l == near(v.total_water_l, 1e-12));
    CHECK(v.sparge_water_l == near(0.0, 1e-12));
}

TEST_CASE("impossible inputs give NaN") {
    BrewVolumeInputs no_volume = twenty_liter_batch();
    no_volume.fermenter_volume_l = 0.0;
    CHECK(std::isnan(plan_brew_volumes(no_volume).total_water_l));

    BrewVolumeInputs full_shrinkage = twenty_liter_batch();
    full_shrinkage.cooling_shrinkage = 1.0;
    CHECK(std::isnan(plan_brew_volumes(full_shrinkage).post_boil_volume_l));

    BrewVolumeInputs negative_loss = twenty_liter_batch();
    negative_loss.kettle_loss_l = -1.0;
    CHECK(std::isnan(plan_brew_volumes(negative_loss).strike_water_l));
}
