#include "domain/packaging.h"

#include "calc/units.h"

#include <algorithm>
#include <cctype>
#include <tuple>

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
    {KegReason::Emptied, "emptied", "Emptied"},
};

constexpr EnumText<PackagedReason> kPackagedTexts[] = {
    {PackagedReason::Packaged,           "packaged",       "Packaged"},
    {PackagedReason::Sold,               "sold",           "Sold"},
    {PackagedReason::Taproom,            "taproom",        "To the taproom"},
    {PackagedReason::Exported,           "exported",       "Exported"},
    {PackagedReason::Supplies,           "supplies",       "Ship or aircraft supplies"},
    {PackagedReason::Research,           "research",       "Research or testing"},
    {PackagedReason::SameOwnership,      "same_ownership", "To another of our breweries"},
    {PackagedReason::Unfit,              "unfit",          "Unfit, used in manufacturing"},
    {PackagedReason::ConsumedOnPremises, "consumed",       "Drunk at the brewery"},
    {PackagedReason::LabSamples,         "lab_samples",    "Lab samples"},
    {PackagedReason::Destroyed,          "destroyed",      "Dumped or destroyed"},
    {PackagedReason::Lost,               "lost",           "Lost or stolen"},
    {PackagedReason::Shortage,           "shortage",       "Count shortage"},
    {PackagedReason::Overage,            "overage",        "Count overage"},
    {PackagedReason::Returned,           "returned",       "Returned"},
};

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

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

bool adds_packages(PackagedReason reason) {
    return reason == PackagedReason::Packaged || reason == PackagedReason::Overage ||
           reason == PackagedReason::Returned;
}

bool keg_comes_back(PackagedReason reason) {
    switch (reason) {
    case PackagedReason::Taproom:
    case PackagedReason::ConsumedOnPremises:
    case PackagedReason::Research:
    case PackagedReason::LabSamples:
    case PackagedReason::Unfit:
    case PackagedReason::Destroyed: return true;
    default:                        return false;
    }
}

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

std::vector<PackagedProduct> combine_by_beer(std::span<const PackagedStock> stock) {
    std::vector<PackagedProduct> products;
    for (const PackagedStock& entry : stock) {
        const auto same = std::find_if(products.begin(), products.end(), [&](const PackagedProduct& product) {
            return product.type == entry.type && product.unit_volume_l == entry.unit_volume_l &&
                   lowercase(product.beer) == lowercase(entry.beer);
        });
        PackagedProduct& product = same != products.end() ? *same : products.emplace_back();
        if (product.batches.empty()) {
            product.beer = entry.beer;
            product.type = entry.type;
            product.unit_volume_l = entry.unit_volume_l;
        }
        product.count += entry.count;
        product.batches.push_back(entry);
    }
    for (PackagedProduct& product : products) {
        std::stable_sort(product.batches.begin(), product.batches.end(),
                         [](const PackagedStock& a, const PackagedStock& b) { return a.packaged_at < b.packaged_at; });
    }
    std::stable_sort(products.begin(), products.end(), [](const PackagedProduct& a, const PackagedProduct& b) {
        return std::tuple(lowercase(a.beer), a.type, a.unit_volume_l) <
               std::tuple(lowercase(b.beer), b.type, b.unit_volume_l);
    });
    return products;
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
