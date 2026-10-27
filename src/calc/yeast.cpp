#include "calc/yeast.h"

#include "calc/common.h"
#include "calc/extract.h"
#include "calc/gravity.h"

#include <algorithm>

namespace ninkasi::calc {

namespace {

constexpr double kMaxWhiteGrowth = 6.0;
constexpr double kMaxCellsPerMl = 200.0;  // million cells/mL: a starter saturates here

double braukaiser_new_cells(double initial_cells_billion, double extract_g) {
    const double cells_per_gram = initial_cells_billion / extract_g;
    double growth_per_gram = 0.0;
    if (cells_per_gram < 1.4) {
        growth_per_gram = 1.4;
    } else if (cells_per_gram < 3.5) {
        growth_per_gram = 2.33 - 0.67 * cells_per_gram;
    }
    return std::max(0.0, growth_per_gram) * extract_g;
}

double white_new_cells(double initial_cells_billion, double volume_l, double agitation_bonus) {
    // Billion cells per liter is million cells per mL.
    const double inoculation_rate = initial_cells_billion / volume_l;
    double growth = 12.54793776 * std::pow(inoculation_rate, -0.4594858324) - 0.9994994906 + agitation_bonus;
    growth = std::clamp(growth, 0.0, kMaxWhiteGrowth);
    // The White model saturates; it never grows a starter past this density.
    const double room = std::max(0.0, kMaxCellsPerMl * volume_l - initial_cells_billion);
    return std::min(initial_cells_billion * growth, room);
}

} // namespace

double cells_needed_billion(double volume_l, double og, double pitch_rate) {
    if (!(volume_l >= 0.0) || !(pitch_rate >= 0.0)) return kInvalid;
    return pitch_rate * volume_l * std::max(0.0, sg_to_plato(og));
}

double liquid_yeast_viability(double days_since_manufacture) {
    if (!(days_since_manufacture >= 0.0)) return kInvalid;
    return std::max(0.0, 1.0 - 0.007 * days_since_manufacture);
}

double starter_cells_billion(double initial_cells_billion, double volume_l, double sg, StarterMethod method) {
    if (!(initial_cells_billion > 0.0) || !(volume_l > 0.0)) return kInvalid;
    const double extract_g = extract_kg(volume_l, sg) * 1000.0;
    if (!(extract_g > 0.0)) return kInvalid;

    double new_cells = 0.0;
    switch (method) {
    case StarterMethod::StirPlate:
        new_cells = braukaiser_new_cells(initial_cells_billion, extract_g);
        break;
    case StarterMethod::Shaken:
        new_cells = white_new_cells(initial_cells_billion, volume_l, 0.5);
        break;
    case StarterMethod::NoAgitation:
        new_cells = white_new_cells(initial_cells_billion, volume_l, 0.0);
        break;
    }
    return initial_cells_billion + new_cells;
}

} // namespace ninkasi::calc
