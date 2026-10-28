#include "domain/packaging.h"

#include "calc/units.h"

#include <algorithm>

namespace ninkasi::domain {

namespace {

template <typename E>
struct EnumText {
    E value;
    std::string_view key;   // stored in the database
    std::string_view name;  // shown to the brewer
};

constexpr EnumText<PackageType> kPackageTexts[] = {
    {PackageType::HalfBarrel, "half_barrel", "Half-barrels"},
    {PackageType::Sixtel,     "sixtel",      "Sixtels"},
    {PackageType::Can,        "can",         "Cans"},
};

constexpr EnumText<KegState> kStateTexts[] = {
    {KegState::Dirty, "dirty", "Dirty"},
    {KegState::Clean, "clean", "Clean"},
};

constexpr EnumText<KegReason> kReasonTexts[] = {
    {KegReason::Added,   "added",   "Added"},
    {KegReason::Cleaned, "cleaned", "Cleaned"},
    {KegReason::Filled,  "filled",  "Filled"},
    {KegReason::Removed, "removed", "Removed"},
};

constexpr EnumText<PackagedReason> kPackagedTexts[] = {
    {PackagedReason::Packaged, "packaged", "Packaged"},
};

template <typename E, std::size_t N>
const EnumText<E>& lookup(E value, const EnumText<E> (&table)[N]) {
    for (const EnumText<E>& entry : table) {
        if (entry.value == value) return entry;
    }
    return table[0];
}

template <typename E, std::size_t N>
std::optional<E> from_key(std::string_view key, const EnumText<E> (&table)[N]) {
    for (const EnumText<E>& entry : table) {
        if (entry.key == key) return entry.value;
    }
    return std::nullopt;
}

} // namespace

bool is_keg(PackageType type) {
    return type == PackageType::HalfBarrel || type == PackageType::Sixtel;
}

double keg_volume_l(PackageType type) {
    switch (type) {
    case PackageType::HalfBarrel: return 15.5 * calc::kLitersPerUsGallon;
    case PackageType::Sixtel:     return 31.0 / 6.0 * calc::kLitersPerUsGallon;  // a sixth of a 31-gallon barrel
    case PackageType::Can:        return 0.0;
    }
    return 0.0;
}

std::string count_text(PackageType type, int count) {
    const char* noun = "";
    switch (type) {
    case PackageType::HalfBarrel: noun = count == 1 ? " half-barrel" : " half-barrels"; break;
    case PackageType::Sixtel:     noun = count == 1 ? " sixtel" : " sixtels"; break;
    case PackageType::Can:        noun = count == 1 ? " can" : " cans"; break;
    }
    return std::to_string(count) + noun;
}

KegDraw plan_keg_draw(KegCounts empty, int count) {
    KegDraw draw;
    if (count <= 0) return draw;
    draw.clean = std::clamp(empty.clean, 0, count);
    draw.dirty = std::clamp(empty.dirty, 0, count - draw.clean);
    draw.unrecorded = count - draw.clean - draw.dirty;
    return draw;
}

std::string_view name(PackageType type) { return lookup(type, kPackageTexts).name; }
std::string_view name(KegState state) { return lookup(state, kStateTexts).name; }
std::string_view name(KegReason reason) { return lookup(reason, kReasonTexts).name; }
std::string_view name(PackagedReason reason) { return lookup(reason, kPackagedTexts).name; }
std::string_view key(PackageType type) { return lookup(type, kPackageTexts).key; }
std::string_view key(KegState state) { return lookup(state, kStateTexts).key; }
std::string_view key(KegReason reason) { return lookup(reason, kReasonTexts).key; }
std::string_view key(PackagedReason reason) { return lookup(reason, kPackagedTexts).key; }

std::optional<PackageType> package_type_from_key(std::string_view text) { return from_key(text, kPackageTexts); }
std::optional<KegState> keg_state_from_key(std::string_view text) { return from_key(text, kStateTexts); }
std::optional<KegReason> keg_reason_from_key(std::string_view text) { return from_key(text, kReasonTexts); }
std::optional<PackagedReason> packaged_reason_from_key(std::string_view text) {
    return from_key(text, kPackagedTexts);
}

} // namespace ninkasi::domain
