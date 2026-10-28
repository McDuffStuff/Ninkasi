#include "calc/formulas.h"

#include <array>
#include <string>
#include <vector>

namespace ninkasi::calc {

namespace {

// --- Inputs ------------------------------------------------------------------------------------
// The examples are a typical 10 bbl ale: they're what the Formulas tab tries
// a formula out with.

constexpr FormulaInput kSgToPlatoInputs[] = {{"SG", "specific gravity", 1.050}};
constexpr FormulaInput kWaterDensityInputs[] = {{"T", "temperature (°F)", 77.0}};
constexpr FormulaInput kHydrometerInputs[] = {
    {"SG_{r}", "the hydrometer's reading", 1.050},
    {"w_{s}", "water density at the sample's temperature (the formula above)", 0.99197},
    {"w_{c}", "water density at the hydrometer's calibration temperature", 1.00085},
};
constexpr FormulaInput kRefractometerWortInputs[] = {
    {"B", "the refractometer's reading (°Brix)", 13.0},
    {"WCF", "wort correction factor", 1.04},
};
constexpr FormulaInput kRefractometerFermentingInputs[] = {
    {"B_{i}", "the original reading divided by the WCF (°Brix)", 12.0},
    {"B_{f}", "the current reading divided by the WCF (°Brix)", 6.5},
};
constexpr FormulaInput kWortCorrectionInputs[] = {
    {"B", "the wort's refractometer reading (°Brix)", 13.0},
    {"P", "the same wort measured with a hydrometer (°P)", 12.5},
};
constexpr FormulaInput kAbvInputs[] = {
    {"OG", "original gravity (SG)", 1.050},
    {"FG", "final gravity (SG)", 1.010},
};
constexpr FormulaInput kRealExtractInputs[] = {
    {"OE", "original extract: the original gravity in °P", 12.4},
    {"AE", "apparent extract: the final gravity in °P", 2.6},
};
constexpr FormulaInput kAlcoholByWeightInputs[] = {
    {"OE", "original extract (°P)", 12.4},
    {"RE", "real extract (°P)", 4.4},
};
constexpr FormulaInput kAbwToAbvInputs[] = {
    {"ABW", "alcohol by weight (%)", 4.1},
    {"FG", "final gravity (SG)", 1.010},
};
constexpr FormulaInput kApparentAttenuationInputs[] = {
    {"OE", "original extract (°P)", 12.4},
    {"AE", "apparent extract (°P)", 2.6},
};
constexpr FormulaInput kRealAttenuationInputs[] = {
    {"OE", "original extract (°P)", 12.4},
    {"RE", "real extract (°P)", 4.4},
};
constexpr FormulaInput kPredictedInputs[] = {
    {"OE", "original extract (°P)", 12.4},
    {"A", "the yeast's apparent attenuation (a fraction)", 0.75},
};
constexpr FormulaInput kUtilizationInputs[] = {
    {"G", "boil gravity (SG)", 1.055},
    {"t", "minutes in the boil", 60.0},
};
constexpr FormulaInput kGaretzUtilizationInputs[] = {
    {"U_{table}", "utilization from Garetz's table for the boil time", 0.21},
    {"GF", "gravity factor", 1.025},
    {"TF", "elevation factor", 1.0},
};
constexpr FormulaInput kGaretzGravityInputs[] = {{"G", "boil gravity (SG)", 1.055}};
constexpr FormulaInput kGaretzElevationInputs[] = {{"h", "the brewery's elevation (ft)", 1000.0}};
constexpr FormulaInput kGaretzHoppingRateInputs[] = {
    {"CF", "concentration factor: volume into the fermenter divided by boil volume", 0.9},
    {"IBU", "the beer's bitterness", 40.0},
};
constexpr FormulaInput kWhirlpoolInputs[] = {{"T", "whirlpool temperature (°C)", 85.0}};
constexpr FormulaInput kAdditionIbuInputs[] = {
    {"U", "utilization, from the formula chosen in the Calculations tab", 0.23},
    {"f_{use}", "first wort, mash or whirlpool factor (1 in the boil)", 1.0},
    {"f_{form}", "hop form factor: pellets, leaf, plugs...", 1.1},
    {"S", "the utilization scale from the Calculations tab", 1.0},
    {"\\alpha", "alpha acids (a fraction)", 0.12},
    {"m", "hop mass (kg)", 0.5},
    {"V", "volume into the fermenter (L)", 1170.0},
};
constexpr FormulaInput kMaltColorInputs[] = {
    {"m_{i}", "each grain's weight (lb)", 0.0},
    {"L_{i}", "its color (°L)", 0.0},
    {"V", "volume into the fermenter (US gal)", 0.0},
};
constexpr FormulaInput kSrmInputs[] = {{"MCU", "malt color units", 10.0}};
constexpr FormulaInput kExtractMassInputs[] = {
    {"V", "volume (L)", 1170.0},
    {"SG", "specific gravity", 1.050},
    {"P", "the same gravity in °P", 12.39},
};
constexpr FormulaInput kPotentialExtractInputs[] = {
    {"m_{i}", "each fermentable's mass (kg)", 0.0},
    {"Y_{i}", "its extract yield (a fraction)", 0.0},
};
constexpr FormulaInput kExpectedExtractInputs[] = {
    {"m_{i}", "each fermentable's mass (kg)", 0.0},
    {"Y_{i}", "its extract yield (a fraction)", 0.0},
    {"\\eta_{i}", "the mash efficiency for mashed grains, 1 for sugars and extracts", 0.0},
};
constexpr FormulaInput kEfficiencyInputs[] = {
    {"E_{actual}", "extract collected (kg)", 150.0},
    {"E_{potential}", "the most the fermentables could give (kg)", 200.0},
};
constexpr FormulaInput kAsIsYieldInputs[] = {
    {"DBFG", "dry-basis fine-grind extract, from the malt analysis (a fraction)", 0.81},
    {"FC", "fine-coarse difference (a fraction)", 0.015},
    {"M", "moisture (a fraction)", 0.04},
};
constexpr FormulaInput kKnockoutInputs[] = {
    {"V_{f}", "volume into the fermenter (L)", 1170.0},
    {"L_{k}", "kettle loss: trub, hops and chiller (L)", 40.0},
};
constexpr FormulaInput kPostBoilInputs[] = {
    {"V_{ko}", "knock-out volume (L)", 1210.0},
    {"s", "cooling shrinkage (a fraction)", 0.04},
};
constexpr FormulaInput kPreBoilInputs[] = {
    {"V_{post}", "volume at the end of the boil (L)", 1260.0},
    {"r", "boil-off rate (L per hour)", 80.0},
    {"t", "boil time (minutes)", 60.0},
};
constexpr FormulaInput kTotalWaterInputs[] = {
    {"V_{pre}", "volume at the start of the boil (L)", 1340.0},
    {"m_{g}", "grain (kg)", 250.0},
    {"a", "grain absorption (L per kg)", 1.0},
    {"L_{m}", "mash tun loss (L)", 20.0},
};
constexpr FormulaInput kStrikeWaterInputs[] = {
    {"m_{g}", "grain (kg)", 250.0},
    {"d", "mash thickness (L per kg)", 2.6},
    {"W", "total water (L)", 1610.0},
};
constexpr FormulaInput kSpargeWaterInputs[] = {
    {"W", "total water (L)", 1610.0},
    {"W_{s}", "strike water (L)", 650.0},
};
constexpr FormulaInput kMashVolumeInputs[] = {
    {"W_{s}", "strike water (L)", 650.0},
    {"m_{g}", "grain (kg)", 250.0},
    {"g", "grain displacement (L per kg)", 0.67},
};
constexpr FormulaInput kSaltIonInputs[] = {
    {"m", "the salt's mass (kg)", 0.5},
    {"M_{salt}", "its molar mass (g/mol)", 172.17},
    {"V", "water volume (L)", 1610.0},
    {"n", "ions of this kind in each unit of the salt", 1.0},
    {"M_{ion}", "the ion's molar mass (g/mol)", 40.078},
};
constexpr FormulaInput kAlkalinityInputs[] = {{"HCO_{3}", "bicarbonate (ppm)", 150.0}};
constexpr FormulaInput kResidualAlkalinityInputs[] = {
    {"HCO_{3}", "bicarbonate (ppm)", 150.0},
    {"Ca", "calcium (ppm)", 50.0},
    {"Mg", "magnesium (ppm)", 10.0},
};
constexpr FormulaInput kSulfateChlorideInputs[] = {
    {"SO_{4}", "sulfate (ppm)", 150.0},
    {"Cl", "chloride (ppm)", 75.0},
};
constexpr FormulaInput kCrystalMaltPhInputs[] = {{"L", "the malt's color (°L)", 40.0}};
constexpr FormulaInput kAcidStrengthInputs[] = {
    {"V", "volume of acid (L)", 0.0},
    {"\\rho", "its density (g/mL)", 0.0},
    {"w", "its strength (a fraction)", 0.0},
    {"M", "the acid's molar mass (g/mol)", 0.0},
    {"n_{H}", "protons each molecule gives up at the mash pH, from its pKa values", 0.0},
};
constexpr FormulaInput kMashPhInputs[] = {
    {"B_{i}", "each grain's buffering (mEq per kg per pH)", 0.0},
    {"m_{i}", "its mass (kg)", 0.0},
    {"pH_{i}", "its pH mashed in distilled water", 0.0},
    {"RA_{meq}", "the mash water's residual alkalinity (mEq)", 0.0},
    {"A", "acid added, at that pH (mEq)", 0.0},
};
constexpr FormulaInput kCellsNeededInputs[] = {
    {"r", "pitch rate (million cells per mL per °P)", 0.75},
    {"V", "batch volume (L)", 1170.0},
    {"P", "original gravity (°P)", 12.4},
};
constexpr FormulaInput kViabilityInputs[] = {{"d", "days since the yeast was made", 30.0}};
constexpr FormulaInput kBraukaiserInputs[] = {
    {"N", "cells pitched into the starter (billion)", 100.0},
    {"E", "extract in the starter (g)", 100.0},
};
constexpr FormulaInput kWhiteInputs[] = {
    {"R", "inoculation rate (million cells per mL)", 50.0},
    {"b", "agitation bonus: 0.5 when shaken, 0 when left still", 0.5},
};
constexpr FormulaInput kExciseInputs[] = {{"B", "barrels removed", 0.0}};

// --- The formulas ------------------------------------------------------------------------------

constexpr FormulaInfo kFormulas[] = {
    // Gravity
    {Formula::SgToPlato, "sg_to_plato", "Gravity", "Specific gravity to °Plato", "°P",
     R"tex(-616.868 + 1111.14\,SG - 630.272\,SG^{2} + 135.997\,SG^{3})tex", kSgToPlatoInputs,
     "A cubic fit to the ASBC sucrose tables. Every gravity shown in °Plato or °Brix uses it, and converting "
     "°P back to SG solves it step by step."},
    {Formula::WaterDensity, "water_density", "Gravity", "Water density by temperature", "w",
     R"tex(1.00130346 - 1.34722124 \times 10^{-4}\,T + 2.04052596 \times 10^{-6}\,T^{2} - 2.32820948 \times 10^{-9}\,T^{3})tex",
     kWaterDensityInputs, "A fit to how water expands as it warms. The hydrometer correction divides two of these."},
    {Formula::HydrometerCorrection, "hydrometer_correction", "Gravity", "Hydrometer temperature correction", "SG",
     R"tex(SG_{r} \cdot \frac{w_{s}}{w_{c}})tex", kHydrometerInputs,
     "A hydrometer reads true only at the temperature it was calibrated for."},
    {Formula::RefractometerWort, "refractometer_wort", "Gravity", "Refractometer reading of unfermented wort", "°P",
     R"tex(\frac{B}{WCF})tex", kRefractometerWortInputs,
     "Refractometers read sugar; wort has other things in it too, which the wort correction factor allows for. "
     "The result is converted to SG."},
    {Formula::RefractometerFermenting, "refractometer_fermenting", "Gravity",
     "Refractometer reading of fermenting beer", "SG",
     R"tex(1 - 0.0044993\,B_{i} + 0.011774\,B_{f} + 0.00027581\,B_{i}^{2} - 0.0012717\,B_{f}^{2} - 0.0000072800\,B_{i}^{3} + 0.000063293\,B_{f}^{3})tex",
     kRefractometerFermentingInputs,
     "Sean Terrill's cubic. Alcohol bends light too, so a reading during fermentation needs the original reading as well."},
    {Formula::WortCorrectionFactor, "wort_correction_factor", "Gravity", "Wort correction factor", "WCF",
     R"tex(\frac{B}{P})tex", kWortCorrectionInputs,
     "Found by measuring the same wort with a refractometer and a hydrometer."},

    // Alcohol and attenuation
    {Formula::AbvSimple, "abv_simple", "Alcohol", "Alcohol by volume, simple", "ABV",
     R"tex((OG - FG) \times 131.25)tex", kAbvInputs, "In %. The everyday formula; it reads a little low for strong beers."},
    {Formula::AbvAlternate, "abv_alternate", "Alcohol", "Alcohol by volume, alternate", "ABV",
     R"tex(\frac{76.08\,(OG - FG)}{1.775 - OG} \cdot \frac{FG}{0.794})tex", kAbvInputs,
     "In %. Hall (Zymurgy, 1995): more accurate for strong beers."},
    {Formula::RealExtract, "real_extract", "Alcohol", "Real extract", "RE",
     R"tex(0.1808\,OE + 0.8192\,AE)tex", kRealExtractInputs,
     "Balling. A hydrometer reads low in beer because alcohol is lighter than water; this is the extract really left."},
    {Formula::AlcoholByWeight, "alcohol_by_weight", "Alcohol", "Alcohol by weight", "ABW",
     R"tex(\frac{OE - RE}{2.0665 - 0.010665\,OE})tex", kAlcoholByWeightInputs,
     "In %. Balling: how much alcohol the extract used up made. The Balling ABV option uses it."},
    {Formula::AbwToAbv, "abw_to_abv", "Alcohol", "Alcohol by weight to by volume", "ABV",
     R"tex(ABW \cdot FG \cdot \frac{0.998203}{0.78924})tex", kAbwToAbvInputs,
     "In %. 0.78924 kg/L is ethanol's density and 0.998203 kg/L water's, at 20 °C."},
    {Formula::ApparentAttenuation, "apparent_attenuation", "Alcohol", "Apparent attenuation", "AA",
     R"tex(\frac{OE - AE}{OE})tex", kApparentAttenuationInputs, "A fraction: 0.75 is 75 %."},
    {Formula::RealAttenuation, "real_attenuation", "Alcohol", "Real attenuation", "RDF",
     R"tex(\frac{OE - RE}{OE})tex", kRealAttenuationInputs,
     "The real degree of fermentation, a fraction: how much of the extract the yeast really ate."},
    {Formula::PredictedFinalExtract, "predicted_final_extract", "Alcohol", "Predicted final gravity", "AE",
     R"tex(OE\,(1 - A))tex", kPredictedInputs,
     "A recipe's final gravity, in °P, from its yeast's attenuation; converted to SG."},

    // Bitterness
    {Formula::TinsethUtilization, "tinseth_utilization", "Bitterness", "Hop utilization, Tinseth", "U",
     R"tex(1.65 \times 0.000125^{G - 1} \times \frac{1 - e^{-0.04\,t}}{4.15})tex", kUtilizationInputs,
     "Glenn Tinseth: the share of a hop's alpha acids that ends up in the beer as bitterness."},
    {Formula::RagerUtilization, "rager_utilization", "Bitterness", "Hop utilization, Rager", "U",
     R"tex(\frac{18.11 + 13.86 \tanh\left(\frac{t - 31.32}{18.27}\right)}{100\left(1 + \max\left(0, \frac{G - 1.050}{0.2}\right)\right)})tex",
     kUtilizationInputs, "Jackie Rager (Zymurgy, 1990), with his correction for worts stronger than 1.050."},
    {Formula::GaretzUtilization, "garetz_utilization", "Bitterness", "Hop utilization, Garetz", "U",
     R"tex(\frac{U_{table}}{GF \cdot TF})tex", kGaretzUtilizationInputs,
     "Mark Garetz (Using Hops). His table gives the utilization by boil time; the bitterness is also divided by "
     "the hopping-rate factor below."},
    {Formula::GaretzGravityFactor, "garetz_gravity_factor", "Bitterness", "Garetz gravity factor", "GF",
     R"tex(1 + \frac{G - 1.050}{0.2})tex", kGaretzGravityInputs, "Stronger worts take up less bitterness."},
    {Formula::GaretzElevationFactor, "garetz_elevation_factor", "Bitterness", "Garetz elevation factor", "TF",
     R"tex(1 + 0.02\,\frac{h}{550})tex", kGaretzElevationInputs,
     "Wort boils cooler at altitude. The brewery's elevation is set in the Calculations tab."},
    {Formula::GaretzHoppingRateFactor, "garetz_hopping_rate_factor", "Bitterness", "Garetz hopping-rate factor",
     "HF", R"tex(1 + \frac{CF}{260}\,IBU)tex", kGaretzHoppingRateInputs,
     "Heavily hopped beers take up less of each addition. It depends on the beer's bitterness, so Ninkasi solves "
     "for the bitterness that agrees with it."},
    {Formula::WhirlpoolFactor, "whirlpool_factor", "Bitterness", "Whirlpool temperature factor", "f",
     R"tex(\min\left(1, 2.39 \times 10^{11}\, e^{-\frac{9773}{T + 273.15}}\right))tex", kWhirlpoolInputs,
     "Mark Hosom (2015): how fast alpha acids isomerize at this temperature compared with a boil. Whirlpool "
     "additions use it as their use factor."},
    {Formula::AdditionIbu, "addition_ibu", "Bitterness", "Bitterness of one hop addition", "IBU",
     R"tex(U \cdot f_{use} \cdot f_{form} \cdot S \cdot \frac{\alpha \cdot m \cdot 10^{6}}{V})tex",
     kAdditionIbuInputs, "A recipe's IBU is the sum over its hop additions. Dry hops add none."},

    // Color
    {Formula::MaltColorUnits, "malt_color_units", "Color", "Malt color units", "MCU",
     R"tex(\frac{\sum_{i} m_{i}\,L_{i}}{V})tex", kMaltColorInputs,
     "Added up over the recipe's grains, so it's shown for reference.", false},
    {Formula::MoreySrm, "morey_srm", "Color", "Beer color, Morey", "SRM", R"tex(1.4922\,MCU^{0.6859})tex",
     kSrmInputs, "Dan Morey's fit, good across the whole range of colors."},
    {Formula::DanielsSrm, "daniels_srm", "Color", "Beer color, Daniels", "SRM", R"tex(\min(MCU, 0.2\,MCU + 8.4))tex",
     kSrmInputs, "Ray Daniels' straight line, for darker beers; pale beers are their MCU."},
    {Formula::MosherSrm, "mosher_srm", "Color", "Beer color, Mosher", "SRM", R"tex(\min(MCU, 0.3\,MCU + 4.7))tex",
     kSrmInputs, "Randy Mosher's straight line, for darker beers; pale beers are their MCU."},

    // Extract and efficiency
    {Formula::ExtractMass, "extract_mass", "Extract", "Extract in wort", "E",
     R"tex(V \cdot SG \cdot 0.998203 \cdot \frac{P}{100})tex", kExtractMassInputs,
     "In kg. 0.998203 kg/L is water's density at 20 °C. Finding the gravity a given extract makes solves this "
     "step by step, as does a malt's potential: the gravity of 1 lb in 1 US gal."},
    {Formula::PotentialExtract, "potential_extract", "Extract", "Potential extract", "E_{potential}",
     R"tex(\sum_{i} m_{i}\,Y_{i})tex", kPotentialExtractInputs,
     "Added up over the recipe's fermentables, so it's shown for reference.", false},
    {Formula::ExpectedExtract, "expected_extract", "Extract", "Expected extract", "E",
     R"tex(\sum_{i} m_{i}\,Y_{i}\,\eta_{i})tex", kExpectedExtractInputs,
     "Added up over the recipe's fermentables, so it's shown for reference. The original gravity is the gravity "
     "this extract makes at knock-out.", false},
    {Formula::Efficiency, "efficiency", "Extract", "Efficiency", "\\eta",
     R"tex(\frac{E_{actual}}{E_{potential}})tex", kEfficiencyInputs, "A fraction: mash or brewhouse efficiency."},
    {Formula::AsIsYield, "as_is_yield", "Extract", "Extract yield as delivered", "Y",
     R"tex((DBFG - FC)(1 - M))tex", kAsIsYieldInputs,
     "Turns a malt analysis into the yield of the grain as it comes, with its moisture."},

    // Brew-day volumes
    {Formula::KnockoutVolume, "knockout_volume", "Volumes", "Knock-out volume", "V_{ko}", R"tex(V_{f} + L_{k})tex",
     kKnockoutInputs, "Brew-day volumes are worked back from the fermenter."},
    {Formula::PostBoilVolume, "post_boil_volume", "Volumes", "Volume at the end of the boil", "V_{post}",
     R"tex(\frac{V_{ko}}{1 - s})tex", kPostBoilInputs, "Hot wort is bigger than cooled wort."},
    {Formula::PreBoilVolume, "pre_boil_volume", "Volumes", "Volume at the start of the boil", "V_{pre}",
     R"tex(V_{post} + r \cdot \frac{t}{60})tex", kPreBoilInputs, ""},
    {Formula::TotalWater, "total_water", "Volumes", "Total water", "W",
     R"tex(V_{pre} + m_{g}\,a + L_{m})tex", kTotalWaterInputs, "Grain keeps some water, and so does the mash tun."},
    {Formula::StrikeWater, "strike_water", "Volumes", "Strike water", "W_{s}", R"tex(\min(m_{g}\,d, W))tex",
     kStrikeWaterInputs, "A thin enough mash uses all the water, leaving none to sparge."},
    {Formula::SpargeWater, "sparge_water", "Volumes", "Sparge water", "W_{sp}", R"tex(W - W_{s})tex",
     kSpargeWaterInputs, ""},
    {Formula::MashVolume, "mash_volume", "Volumes", "Mash volume", "V_{mash}", R"tex(W_{s} + m_{g}\,g)tex",
     kMashVolumeInputs, "How much room the mash takes in the tun."},

    // Water
    {Formula::SaltIons, "salt_ions", "Water", "Ions from a salt addition", "c_{ion}",
     R"tex(\frac{m \cdot 10^{6}}{M_{salt}\,V} \cdot n \cdot M_{ion})tex", kSaltIonInputs,
     "In ppm (mg/L), added to the water's own. Gypsum, for example, gives one calcium and one sulfate for each "
     "unit of salt."},
    {Formula::Alkalinity, "alkalinity", "Water", "Alkalinity", "\\mathrm{Alk}",
     R"tex(HCO_{3} \cdot \frac{50.043}{61.016})tex", kAlkalinityInputs,
     "In ppm as CaCO3. 61.016 g/mol is bicarbonate's molar mass, 50.043 g/mol calcium carbonate's equivalent mass."},
    {Formula::ResidualAlkalinity, "residual_alkalinity", "Water", "Residual alkalinity", "RA",
     R"tex(50.043\left(\frac{HCO_{3}}{61.016} - \frac{Ca}{3.5 \cdot 20.039} - \frac{Mg}{7 \cdot 12.1525}\right))tex",
     kResidualAlkalinityInputs,
     "Kolbach, in ppm as CaCO3: the alkalinity left after calcium and magnesium react with the malt's phosphates."},
    {Formula::SulfateChlorideRatio, "sulfate_chloride_ratio", "Water", "Sulfate to chloride ratio",
     "SO_{4} : Cl", R"tex(\frac{SO_{4}}{Cl})tex", kSulfateChlorideInputs,
     "High is crisp and bitter, low is full and malty."},
    {Formula::CrystalMaltPh, "crystal_malt_ph", "Water", "Distilled-water pH of crystal malt", "pH_{DI}",
     R"tex(\max(4.5, \min(5.6, 5.38 - 0.0144\,(L - 10))))tex", kCrystalMaltPhInputs,
     "A rough fit, used when a crystal malt has no measured pH. Base malt is taken as 5.7 and roasted malt 4.7."},
    {Formula::AcidStrength, "acid_strength", "Water", "Acid added to the mash", "A",
     R"tex(10^{6}\,\frac{V\,\rho\,w}{M}\,n_{H})tex", kAcidStrengthInputs,
     "In mEq. How many protons each molecule gives up depends on the mash pH, so it's worked out from the "
     "acid's pKa values and shown for reference. Acidulated malt counts as 3 % lactic acid.", false},
    {Formula::MashPh, "mash_ph", "Water", "Mash pH", "pH",
     R"tex(\frac{\sum_{i} B_{i}\,m_{i}\,pH_{i} + RA_{meq} - A}{\sum_{i} B_{i}\,m_{i}})tex", kMashPhInputs,
     "A charge balance after Kai Troester: each malt resists moving from its own pH. The acid depends on the pH "
     "being found, so it's solved step by step and shown for reference.", false},

    // Yeast
    {Formula::CellsNeeded, "cells_needed", "Yeast", "Yeast cells needed", "N",
     R"tex(r \cdot V \cdot \max(0, P))tex", kCellsNeededInputs,
     "In billions. Ales usually take 0.75 and lagers 1.5 million cells per mL per °P."},
    {Formula::LiquidYeastViability, "liquid_yeast_viability", "Yeast", "Liquid yeast viability", "v",
     R"tex(\max(0, 1 - 0.007\,d))tex", kViabilityInputs, "A fraction: liquid yeast loses about 0.7 % a day."},
    {Formula::BraukaiserGrowth, "braukaiser_growth", "Yeast", "Starter growth on a stir plate", "N_{new}",
     R"tex(E \cdot \max\left(0, \begin{cases} 1.4 & \text{if } \frac{N}{E} < 1.4 \\ 2.33 - 0.67\,\frac{N}{E} & \text{if } \frac{N}{E} < 3.5 \\ 0 & \text{otherwise} \end{cases}\right))tex",
     kBraukaiserInputs,
     "Kai Troester (Braukaiser): new cells, in billions, depend on how many cells there are for each gram of "
     "extract."},
    {Formula::WhiteGrowth, "white_growth", "Yeast", "Starter growth, shaken or still", "g",
     R"tex(\min\left(6, \max\left(0, 12.54793776\,R^{-0.4594858324} - 0.9994994906 + b\right)\right))tex",
     kWhiteInputs,
     "Chris White and Jamil Zainasheff (Yeast): the starter grows to this many times the cells pitched, and no "
     "denser than 200 million cells per mL."},

    // TTB
    {Formula::ExciseTax, "excise_tax", "TTB", "Federal excise tax", "\\text{Tax}",
     R"tex(\begin{cases} 3.50\,B & \text{for the year's first 60,000 barrels (small brewers)} \\ 16.00\,B & \text{up to 6,000,000 barrels} \\ 18.00\,B & \text{beyond that} \end{cases})tex",
     kExciseInputs,
     "In dollars, by 26 U.S.C. 5051. The bands count barrels from the start of the calendar year. Set by law, so "
     "it's shown for reference.", false},
};

static_assert(std::size(kFormulas) == kFormulaCount, "every formula needs a row");

constexpr bool rows_follow_enum_order() {
    for (std::size_t i = 0; i < std::size(kFormulas); ++i) {
        if (static_cast<std::size_t>(kFormulas[i].formula) != i) return false;
    }
    return true;
}
static_assert(rows_follow_enum_order(), "the formula table must follow the enum's order");

std::array<std::optional<Expression>, kFormulaCount>& adjustments() {
    static std::array<std::optional<Expression>, kFormulaCount> table;
    return table;
}

} // namespace

std::span<const FormulaInfo> formulas() {
    return kFormulas;
}

const FormulaInfo& info(Formula formula) {
    return kFormulas[static_cast<std::size_t>(formula)];
}

std::optional<Formula> formula_from_key(std::string_view key) {
    for (const FormulaInfo& entry : kFormulas) {
        if (entry.key == key) return entry.formula;
    }
    return std::nullopt;
}

Expression compile_formula(Formula formula, std::string_view latex) {
    const FormulaInfo& entry = info(formula);
    if (!entry.adjustable) throw FormulaError(std::string(entry.name) + " is shown for reference and can't be adjusted.");
    std::vector<std::string_view> symbols;
    for (const FormulaInput& input : entry.inputs) symbols.push_back(input.symbol);
    return Expression::compile(latex, symbols);
}

void set_adjusted_formula(Formula formula, std::optional<Expression> adjusted) {
    adjustments()[static_cast<std::size_t>(formula)] = std::move(adjusted);
}

void clear_adjusted_formulas() {
    for (std::optional<Expression>& adjusted : adjustments()) adjusted.reset();
}

const Expression* adjusted_formula(Formula formula) {
    const std::optional<Expression>& adjusted = adjustments()[static_cast<std::size_t>(formula)];
    return adjusted ? &*adjusted : nullptr;
}

} // namespace ninkasi::calc
