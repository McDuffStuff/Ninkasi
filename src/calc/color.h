#pragma once
// Beer color estimation.
//
// Malt color units (MCU) = Σ(malt °L × lb) / US gal, then one of:
//   Morey    SRM = 1.4922 · MCU^0.6859   (default; fits the full range)
//   Daniels  SRM = 0.2 · MCU + 8.4
//   Mosher   SRM = 0.3 · MCU + 4.7
// Daniels' and Mosher's straight lines are fits for darker beers; below the
// point where each line crosses SRM = MCU, Ninkasi uses MCU itself so pale
// beers aren't given color they don't have.

#include <span>

namespace ninkasi::calc {

enum class ColorModel { Morey, Daniels, Mosher };

inline constexpr double kEbcPerSrm = 1.97;
inline double srm_to_ebc(double srm) { return srm * kEbcPerSrm; }
inline double ebc_to_srm(double ebc) { return ebc / kEbcPerSrm; }

struct GrainColor {
    double mass_kg = 0.0;
    double color_lovibond = 0.0;
};

double malt_color_units(std::span<const GrainColor> grains, double volume_l);
double srm_from_mcu(double mcu, ColorModel model);
double estimate_srm(std::span<const GrainColor> grains, double volume_l, ColorModel model = ColorModel::Morey);

// Approximate on-screen color of beer at `srm`, for swatches and tank fills
// (channels 0–1). A common exponential approximation, not a measurement:
//   R = 0.975^SRM, G = 0.96 · 0.88^SRM, B = 0.86 · 0.7^SRM
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};
Rgb srm_to_rgb(double srm);

} // namespace ninkasi::calc
