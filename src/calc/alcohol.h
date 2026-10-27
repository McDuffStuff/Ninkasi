#pragma once
// Alcohol content and attenuation.
//
// Attenuation is computed from extract (°P), the brewing-chemistry
// definition: apparent attenuation = (OE − AE) / OE. Homebrew software often
// uses gravity points instead, (OG − FG) / (OG − 1), which reads ~0.5 % higher.

namespace ninkasi::calc {

enum class AbvFormula {
    Simple,     // (OG − FG) × 131.25, the common homebrew formula
    Alternate,  // Hall (Zymurgy, 1995): more accurate for strong beers
    Balling,    // via real extract and alcohol by weight (classic brewing chemistry)
};

// Alcohol by volume, as a fraction (0.0525 = 5.25 %). An FG above the OG
// gives a negative result rather than hiding the bad reading.
double abv(double og, double fg, AbvFormula formula = AbvFormula::Simple);

// Real extract in °P, by Balling: RE = 0.1808 · OE + 0.8192 · AE.
double real_extract_plato(double og, double fg);

// Alcohol by weight as a fraction, by Balling: (OE − RE) / (2.0665 − 0.010665 · OE).
double abw(double og, double fg);
double abw_to_abv(double alcohol_by_weight, double fg);

// Apparent and real degree of fermentation, as fractions.
double apparent_attenuation(double og, double fg);
double real_attenuation(double og, double fg);

// Final gravity expected when the yeast reaches `attenuation` (apparent).
double predict_fg(double og, double attenuation);

} // namespace ninkasi::calc
