#pragma once
// Packaged beer and the kegs it goes into.
//
// Beer is packaged into half-barrel kegs, sixtels or cans, and from then on
// is counted in packages rather than measured in barrels. Kegs belong to the
// brewery and go round: empty kegs wait in the keg cleaner, dirty until
// they're cleaned, and are filled when beer is packaged. Cans are stock,
// drawn from inventory when they're filled.

#include "domain/timestamp.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

enum class PackageType {
    HalfBarrel,  // a half-barrel keg: 15.5 US gallons
    Sixtel,      // a sixth-barrel keg: 5.17 US gallons
    Can,         // what a can holds comes from its inventory item
};

inline constexpr std::array kPackageTypes = {PackageType::HalfBarrel, PackageType::Sixtel, PackageType::Can};
inline constexpr std::array kKegTypes = {PackageType::HalfBarrel, PackageType::Sixtel};

bool is_keg(PackageType type);
// What a keg holds, in liters; 0 for cans.
double keg_volume_l(PackageType type);
// "1 half-barrel", "20 sixtels", "480 cans".
std::string count_text(PackageType type, int count);

// --- Kegs -------------------------------------------------------------------

// Empty kegs are dirty or clean.
enum class KegState { Dirty, Clean };

// Why the number of empty kegs changed.
enum class KegReason {
    Added,    // new kegs, or kegs back from customers: they arrive dirty
    Cleaned,  // dirty kegs cleaned (a pair of rows: out of dirty, into clean)
    Filled,   // filled when beer was packaged
    Removed,  // lost, damaged or retired
};

// One row of the keg ledger.
struct KegMovement {
    std::int64_t id = 0;
    Timestamp at{};
    PackageType type = PackageType::HalfBarrel;  // a keg type
    KegState state = KegState::Dirty;
    int quantity = 0;                            // positive in, negative out
    KegReason reason = KegReason::Added;
    std::optional<std::int64_t> batch_id;        // filled with
    std::string note;
    std::string user;                            // who recorded it, when read back
};

// How many empty kegs of one size there are.
struct KegCounts {
    int dirty = 0;
    int clean = 0;
};

// Some kegs of one size: kegs being added, cleaned or removed.
struct KegCount {
    PackageType type = PackageType::HalfBarrel;
    int count = 0;
};

// Where the kegs for a fill come from: clean kegs first; then dirty ones,
// which must have been cleaned without it being logged; then, if there
// still aren't enough, kegs the keg cleaner didn't know about.
struct KegDraw {
    int clean = 0;
    int dirty = 0;
    int unrecorded = 0;

    bool operator==(const KegDraw&) const = default;
};
KegDraw plan_keg_draw(KegCounts empty, int count);

// --- Packaged beer ------------------------------------------------------------

// Why the number of packages of beer changed.
enum class PackagedReason {
    Packaged,  // filled from a tank
};

// One row of the packaged goods ledger.
struct PackagedMovement {
    std::int64_t id = 0;
    Timestamp at{};
    std::int64_t batch_id = 0;
    PackageType type = PackageType::HalfBarrel;
    double unit_volume_l = 0.0;             // what each package holds
    int quantity = 0;                       // packages: positive in, negative out
    PackagedReason reason = PackagedReason::Packaged;
    std::optional<std::int64_t> vessel_id;  // packaged from
    std::string note;
    std::string user;                       // who recorded it, when read back
};

// Packaged beer on hand: one batch in one kind and size of package.
struct PackagedStock {
    std::int64_t batch_id = 0;
    std::string batch_code;
    std::string beer;
    PackageType type = PackageType::HalfBarrel;
    double unit_volume_l = 0.0;
    int count = 0;
    Timestamp packaged_at{};  // when the first of them was packaged

    double volume_l() const { return unit_volume_l * count; }
};

// One beer in one kind and size of package, from however many batches.
struct PackagedProduct {
    std::string beer;
    PackageType type = PackageType::HalfBarrel;
    double unit_volume_l = 0.0;
    int count = 0;                       // of all its batches
    std::vector<PackagedStock> batches;  // oldest first

    double volume_l() const { return unit_volume_l * count; }
};

// Packaged beer combined by beer (by name, ignoring case) and package (kind
// and size), by beer name and then kegs before cans.
std::vector<PackagedProduct> combine_by_beer(std::span<const PackagedStock> stock);

// --- Names and stored keys ----------------------------------------------------
// Keys are stored in the database: never change one once shipped.

std::string_view name(PackageType type);  // "Half-barrels", "Sixtels", "Cans"
std::string_view name(KegState state);
std::string_view name(KegReason reason);
std::string_view name(PackagedReason reason);
std::string_view key(PackageType type);
std::string_view key(KegState state);
std::string_view key(KegReason reason);
std::string_view key(PackagedReason reason);
std::optional<PackageType> package_type_from_key(std::string_view text);
std::optional<KegState> keg_state_from_key(std::string_view text);
std::optional<KegReason> keg_reason_from_key(std::string_view text);
std::optional<PackagedReason> packaged_reason_from_key(std::string_view text);

} // namespace ninkasi::domain
