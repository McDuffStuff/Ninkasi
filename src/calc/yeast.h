#pragma once
// Yeast: how many cells to pitch, how many are still alive, and how many a
// starter grows.
//
// Pitch rates are in million cells per mL of wort per °Plato, the standard
// popularized by Fix and by Mr Malty: about 0.75 for ales and 1.5 for lagers,
// more for strong worts.

namespace ninkasi::calc {

inline constexpr double kAlePitchRate = 0.75;
inline constexpr double kLagerPitchRate = 1.5;

// Billions of cells to pitch into `volume_l` of wort at gravity `og`.
// (1 million cells/mL/°P is 1 billion cells/L/°P.)
double cells_needed_billion(double volume_l, double og, double pitch_rate);

// Fraction of a liquid yeast pack still viable `days` after manufacture:
// the common rule of a linear 0.7 % loss per day (about 21 % a month).
double liquid_yeast_viability(double days_since_manufacture);

enum class StarterMethod {
    StirPlate,    // Braukaiser model
    Shaken,       // White model, plus 0.5 growth for intermittent shaking
    NoAgitation,  // White model
};

// Billions of cells after growing `initial_cells_billion` in a starter of
// `volume_l` wort at gravity `sg`.
//
// Stir plate (Braukaiser): new cells per gram of extract depend on the cells
// per gram pitched (c): 1.4 billion if c < 1.4; 2.33 − 0.67·c up to 3.5;
// none beyond.
// Otherwise (White & Zainasheff, "Yeast"): new cells = initial × G, with
// G = 12.54793776 · x^−0.4594858324 − 0.9994994906 for an inoculation rate x
// in million cells/mL, capped at 6, never growing past 200 million cells/mL.
double starter_cells_billion(double initial_cells_billion, double volume_l, double sg, StarterMethod method);

} // namespace ninkasi::calc
