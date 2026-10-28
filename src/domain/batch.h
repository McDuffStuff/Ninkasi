#pragma once
// Batches of beer and what happens to them: brewhouse turns, the ingredients
// actually used, readings, and every movement of beer in and out of vessels.

#include "domain/element.h"
#include "domain/recipe.h"
#include "domain/timestamp.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

// --- Brewing ----------------------------------------------------------------

// One brewhouse turn. Most batches have one; filling a large fermenter can
// take several.
struct Brew {
    std::int64_t id = 0;
    std::int64_t batch_id = 0;
    std::optional<std::int64_t> brewhouse_id;
    std::optional<std::int64_t> vessel_id;    // knocked out into
    Timestamp started_at{};
    std::optional<Timestamp> knocked_out_at;  // nullopt while brewing
    double volume_l = 0.0;                    // into the vessel
    std::optional<double> og;                 // measured
    std::string notes;

    bool in_progress() const { return !knocked_out_at.has_value(); }
};

enum class IngredientKind { Fermentable, Hop, Yeast, Misc };

// How an ingredient's amount is measured.
enum class AmountKind {
    Mass,    // kilograms
    Volume,  // liters (yeast slurry)
    Count,   // packs (liquid yeast)
    Text,    // free text, for other additions ("1 tablet")
};

// One ingredient a brew used, as confirmed by the brewer at knock-out.
struct IngredientUse {
    IngredientKind kind = IngredientKind::Fermentable;
    std::int64_t library_id = 0;  // the library entry it came from; 0 if none
    std::string name;
    std::string detail;           // which addition: "Boil, 60 min", "Dry hop"...
    AmountKind amount_kind = AmountKind::Mass;
    double planned = 0.0;         // what the recipe called for
    double actual = 0.0;          // what was used
    std::string text;             // Text amounts: what was used

    bool operator==(const IngredientUse&) const = default;
};

// Every ingredient of `recipe`, in recipe order, with the recipe's amounts
// as both planned and actual.
std::vector<IngredientUse> planned_ingredients(const Recipe& recipe);

// --- Readings ---------------------------------------------------------------

enum class ReadingMethod { Hydrometer, Refractometer, Digital };

// A gravity, temperature and/or pH reading of a batch.
struct Reading {
    std::int64_t id = 0;
    std::int64_t batch_id = 0;
    std::optional<std::int64_t> vessel_id;  // where the sample came from
    Timestamp at{};
    ReadingMethod method = ReadingMethod::Hydrometer;
    std::optional<double> gravity_sg;       // corrected: the actual gravity
    std::optional<double> raw_value;        // as read: hydrometer SG or refractometer °Brix
    std::optional<double> temperature_c;
    std::optional<double> ph;
    std::string note;
};

// --- Batches ----------------------------------------------------------------

enum class BatchStatus {
    Brewing,       // brewed, but no knock-out logged yet: nothing is in a vessel
    Fermenting,
    Conditioning,  // crashing, lagering or in a brite tank
    Packaged,      // finished: all of it has left the vessels, some packaged
    Dumped,        // finished: none of it was packaged
};

// Brewing, fermenting or conditioning: not finished.
bool is_active(BatchStatus status);

struct Batch {
    std::int64_t id = 0;
    std::string code;                       // the brewery's batch number, e.g. "142"
    std::optional<std::int64_t> recipe_id;  // the recipe it was brewed from, while that exists
    Recipe recipe;                          // the recipe as brewed; its name is the beer's
    BatchStatus status = BatchStatus::Brewing;
    Timestamp created_at{};
    std::optional<Timestamp> finished_at;
    std::vector<Brew> brews;        // oldest first
    std::vector<Reading> readings;  // oldest first

    const std::string& name() const { return recipe.name; }
    // Measured original gravity: the knocked-out brews' OGs averaged by volume.
    std::optional<double> og() const;
    // When the first brew was knocked out.
    std::optional<Timestamp> brewed_at() const;
    const Brew* brew_in_progress() const;
    // The most recent reading with a gravity, or with a temperature.
    const Reading* latest_gravity() const;
    const Reading* latest_temperature() const;
};

// "#142 Hazy IPA", for lists and the activity log.
std::string batch_label(std::string_view code, std::string_view name);
inline std::string label(const Batch& batch) { return batch_label(batch.code, batch.name()); }

// The code after `previous`: its trailing number plus one, keeping any
// prefix and zero padding ("26-009" → "26-010", "B7" → "B8"). "1" if there
// is no previous code; `previous` + "-2" if it doesn't end in a number.
std::string next_batch_code(std::string_view previous);

// --- Vessels and the volume ledger --------------------------------------------

// Fermenters and brite tanks hold beer between brew day and packaging.
bool holds_beer(ElementType type);

enum class VesselCondition { Clean, Dirty, OutOfService };

enum class PackageFormat { Kegs, CansOrBottles };

// Why the volume of beer in a vessel changed. These are the ledger's reason
// codes, which TTB reports will be built from.
enum class VolumeReason {
    Brewed,       // knocked out from the brewhouse
    TransferIn,
    TransferOut,
    Kegged,
    Bottled,      // cans and bottles
    Loss,         // left behind by a transfer or packaging run
    Dumped,
    Adjusted,     // a measured volume replaced the recorded one
};

// One change in the volume of a batch in a vessel.
struct VolumeEvent {
    std::int64_t id = 0;
    Timestamp at{};
    std::int64_t batch_id = 0;
    std::optional<std::int64_t> vessel_id;
    double volume_l = 0.0;  // positive into the vessel, negative out of it
    VolumeReason reason = VolumeReason::Adjusted;
};

// Volumes closer to zero than this count as empty (floating-point sums of
// the ledger rarely land exactly on zero).
inline constexpr double kEmptyVolumeL = 0.001;

// --- Names and stored keys ----------------------------------------------------
// Keys are stored in the database: never change one once shipped.

inline constexpr std::array kReadingMethods = {ReadingMethod::Hydrometer, ReadingMethod::Refractometer,
                                               ReadingMethod::Digital};
inline constexpr std::array kPackageFormats = {PackageFormat::Kegs, PackageFormat::CansOrBottles};

std::string_view name(BatchStatus status);
std::string_view name(ReadingMethod method);
std::string_view name(VesselCondition condition);
std::string_view name(PackageFormat format);
std::string_view name(VolumeReason reason);

std::string_view key(BatchStatus status);
std::string_view key(IngredientKind kind);
std::string_view key(AmountKind kind);
std::string_view key(ReadingMethod method);
std::string_view key(VesselCondition condition);
std::string_view key(VolumeReason reason);

std::optional<BatchStatus> batch_status_from_key(std::string_view text);
std::optional<IngredientKind> ingredient_kind_from_key(std::string_view text);
std::optional<AmountKind> amount_kind_from_key(std::string_view text);
std::optional<ReadingMethod> reading_method_from_key(std::string_view text);
std::optional<VesselCondition> vessel_condition_from_key(std::string_view text);
std::optional<VolumeReason> volume_reason_from_key(std::string_view text);

} // namespace ninkasi::domain
