#include "calc/extract.h"

#include "calc/common.h"
#include "calc/formulas.h"
#include "calc/gravity.h"
#include "calc/units.h"

namespace ninkasi::calc {

double extract_kg(double volume_l, double sg) {
    if (!(volume_l >= 0.0)) return kInvalid;
    const double plato = sg_to_plato(sg);
    return with_formula(Formula::ExtractMass, {volume_l, sg, plato}, [&] {
        const double wort_mass_kg = volume_l * sg * kWaterDensityKgPerL;
        return wort_mass_kg * plato / 100.0;
    });
}

double sg_from_extract(double extract_mass_kg, double volume_l) {
    if (!(volume_l > 0.0) || !(extract_mass_kg >= 0.0)) return kInvalid;
    if (adjusted_formula(Formula::ExtractMass)) {
        // An adjusted extract formula is solved by halving the range the gravity can be in.
        double low = 0.5;
        double high = 2.0;
        for (int i = 0; i < 100; ++i) {
            const double middle = (low + high) / 2.0;
            if (extract_kg(volume_l, middle) < extract_mass_kg) {
                low = middle;
            } else {
                high = middle;
            }
        }
        return (low + high) / 2.0;
    }
    // extract = volume × water density × SG × °P / 100, and °P depends on SG,
    // so iterate: guess SG, work out the °P that implies, convert back.
    // Each step shrinks the error ~10x or more, so this converges quickly.
    const double sg_times_plato = 100.0 * extract_mass_kg / (volume_l * kWaterDensityKgPerL);
    double sg = 1.0;
    for (int i = 0; i < 100; ++i) {
        const double next = plato_to_sg(sg_times_plato / sg);
        if (std::abs(next - sg) < 1e-13) return next;
        sg = next;
    }
    return sg;
}

double potential_extract_kg(std::span<const Fermentable> fermentables) {
    double total = 0.0;
    for (const Fermentable& f : fermentables) {
        if (!(f.mass_kg >= 0.0) || !(f.yield >= 0.0)) return kInvalid;
        total += f.mass_kg * f.yield;
    }
    return total;
}

double expected_extract_kg(std::span<const Fermentable> fermentables, double mash_efficiency) {
    if (!(mash_efficiency >= 0.0)) return kInvalid;
    double total = 0.0;
    for (const Fermentable& f : fermentables) {
        if (!(f.mass_kg >= 0.0) || !(f.yield >= 0.0)) return kInvalid;
        total += f.mass_kg * f.yield * (f.mashed ? mash_efficiency : 1.0);
    }
    return total;
}

double efficiency(double actual_kg, double potential_kg) {
    if (!(potential_kg > 0.0) || !(actual_kg >= 0.0)) return kInvalid;
    return with_formula(Formula::Efficiency, {actual_kg, potential_kg}, [&] { return actual_kg / potential_kg; });
}

double as_is_yield(double dry_basis_extract, double moisture, double fine_coarse_difference) {
    return with_formula(Formula::AsIsYield, {dry_basis_extract, fine_coarse_difference, moisture},
                        [&] { return (dry_basis_extract - fine_coarse_difference) * (1.0 - moisture); });
}

double potential_sg_from_yield(double yield) {
    if (!(yield >= 0.0)) return kInvalid;
    return sg_from_extract(yield * kKilogramsPerPound, kLitersPerUsGallon);
}

double yield_from_potential_sg(double potential_sg) {
    return extract_kg(kLitersPerUsGallon, potential_sg) / kKilogramsPerPound;
}

} // namespace ninkasi::calc
