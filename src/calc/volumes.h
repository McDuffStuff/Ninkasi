#pragma once
// Brew-day volume planning: working back from the volume wanted in the
// fermenter to the water needed and the volume to expect at each step.
//
//   water → mash → pre-boil →(boil-off)→ post-boil →(cooling)→ knockout
//         →(kettle losses)→ fermenter
//
// Volumes are not corrected for thermal expansion except across cooling:
// cooling_shrinkage covers hot post-boil wort contracting to pitching
// temperature. Calibrate the losses against your own brew days.

namespace ninkasi::calc {

struct BrewVolumeInputs {
    double fermenter_volume_l = 0.0;            // wort wanted in the fermenter
    double kettle_loss_l = 0.0;                 // trub, hops, chiller and kettle dead space left behind
    double cooling_shrinkage = 0.04;            // fraction wort contracts as it cools (≈ 4 %)
    double boil_off_l_per_hr = 0.0;
    double boil_time_min = 60.0;
    double grain_mass_kg = 0.0;
    double grain_absorption_l_per_kg = 1.0;     // wort held by spent grain (≈ 1.0 L/kg, 0.12 gal/lb)
    double mash_tun_loss_l = 0.0;               // dead space under the false bottom
    double mash_thickness_l_per_kg = 3.0;       // strike water per kg of grain (≈ 1.4 qt/lb)
    double grain_displacement_l_per_kg = 0.67;  // volume grain adds to the mash (≈ 0.08 gal/lb)
};

struct BrewVolumes {
    double strike_water_l = 0.0;
    double sparge_water_l = 0.0;
    double total_water_l = 0.0;
    double mash_volume_l = 0.0;       // strike water + grain: check it fits the mash tun
    double pre_boil_volume_l = 0.0;
    double post_boil_volume_l = 0.0;
    double knockout_volume_l = 0.0;   // after cooling, before kettle losses
    double fermenter_volume_l = 0.0;
};

// Every field is kInvalid if the inputs are impossible: no fermenter volume,
// negative amounts, or shrinkage of 100 % or more.
BrewVolumes plan_brew_volumes(const BrewVolumeInputs& inputs);

} // namespace ninkasi::calc
