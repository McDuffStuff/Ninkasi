#include "calc/volumes.h"

#include "calc/common.h"

#include <algorithm>

namespace ninkasi::calc {

namespace {

bool inputs_are_possible(const BrewVolumeInputs& in) {
    const double amounts[] = {
        in.kettle_loss_l,   in.boil_off_l_per_hr,         in.boil_time_min,
        in.grain_mass_kg,   in.grain_absorption_l_per_kg, in.mash_tun_loss_l,
        in.mash_thickness_l_per_kg, in.grain_displacement_l_per_kg,
    };
    for (double amount : amounts) {
        if (!(amount >= 0.0)) return false;
    }
    return in.fermenter_volume_l > 0.0 && in.cooling_shrinkage >= 0.0 && in.cooling_shrinkage < 1.0;
}

} // namespace

BrewVolumes plan_brew_volumes(const BrewVolumeInputs& in) {
    if (!inputs_are_possible(in)) {
        return {kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid};
    }

    BrewVolumes v;
    v.fermenter_volume_l = in.fermenter_volume_l;
    v.knockout_volume_l = in.fermenter_volume_l + in.kettle_loss_l;
    v.post_boil_volume_l = v.knockout_volume_l / (1.0 - in.cooling_shrinkage);
    v.pre_boil_volume_l = v.post_boil_volume_l + in.boil_off_l_per_hr * in.boil_time_min / 60.0;

    v.total_water_l = v.pre_boil_volume_l + in.grain_mass_kg * in.grain_absorption_l_per_kg + in.mash_tun_loss_l;
    // A thin enough mash can use all the water; then there's nothing to sparge.
    v.strike_water_l = std::min(in.grain_mass_kg * in.mash_thickness_l_per_kg, v.total_water_l);
    v.sparge_water_l = v.total_water_l - v.strike_water_l;
    v.mash_volume_l = v.strike_water_l + in.grain_mass_kg * in.grain_displacement_l_per_kg;
    return v;
}

} // namespace ninkasi::calc
