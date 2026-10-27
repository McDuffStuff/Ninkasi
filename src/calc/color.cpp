#include "calc/color.h"

#include "calc/common.h"
#include "calc/units.h"

#include <algorithm>

namespace ninkasi::calc {

double malt_color_units(std::span<const GrainColor> grains, double volume_l) {
    if (!(volume_l > 0.0)) return kInvalid;
    double lovibond_pounds = 0.0;
    for (const GrainColor& grain : grains) {
        if (!(grain.mass_kg >= 0.0) || !(grain.color_lovibond >= 0.0)) return kInvalid;
        lovibond_pounds += grain.color_lovibond * grain.mass_kg / kKilogramsPerPound;
    }
    return lovibond_pounds / (volume_l / kLitersPerUsGallon);
}

double srm_from_mcu(double mcu, ColorModel model) {
    if (!(mcu >= 0.0)) return kInvalid;
    switch (model) {
    case ColorModel::Morey:
        return 1.4922 * std::pow(mcu, 0.6859);
    case ColorModel::Daniels:
        return std::min(mcu, 0.2 * mcu + 8.4);
    case ColorModel::Mosher:
        return std::min(mcu, 0.3 * mcu + 4.7);
    }
    return kInvalid;
}

double estimate_srm(std::span<const GrainColor> grains, double volume_l, ColorModel model) {
    return srm_from_mcu(malt_color_units(grains, volume_l), model);
}

Rgb srm_to_rgb(double srm) {
    const double s = is_valid(srm) ? std::max(0.0, srm) : 0.0;
    return {static_cast<float>(std::pow(0.975, s)), static_cast<float>(245.0 / 255.0 * std::pow(0.88, s)),
            static_cast<float>(220.0 / 255.0 * std::pow(0.7, s))};
}

} // namespace ninkasi::calc
