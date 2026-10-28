#include "domain/batch.h"

#include <cstdio>

namespace ninkasi::domain {

namespace {

template <typename E>
struct EnumText {
    E value;
    std::string_view key;   // stored in the database
    std::string_view name;  // shown to the brewer
};

constexpr EnumText<BatchStatus> kBatchStatuses[] = {
    {BatchStatus::Brewing,      "brewing",      "Awaiting knock-out"},
    {BatchStatus::Fermenting,   "fermenting",   "Fermenting"},
    {BatchStatus::Conditioning, "conditioning", "Conditioning"},
    {BatchStatus::Packaged,     "packaged",     "Packaged"},
    {BatchStatus::Dumped,       "dumped",       "Dumped"},
};

constexpr EnumText<IngredientKind> kIngredientKinds[] = {
    {IngredientKind::Fermentable, "fermentable", "Fermentable"},
    {IngredientKind::Hop,         "hop",         "Hop"},
    {IngredientKind::Yeast,       "yeast",       "Yeast"},
    {IngredientKind::Misc,        "misc",        "Other"},
};

constexpr EnumText<AmountKind> kAmountKinds[] = {
    {AmountKind::Mass,   "mass",   "Mass"},
    {AmountKind::Volume, "volume", "Volume"},
    {AmountKind::Count,  "count",  "Count"},
    {AmountKind::Text,   "text",   "Text"},
};

constexpr EnumText<ReadingMethod> kReadingMethodTexts[] = {
    {ReadingMethod::Hydrometer,    "hydrometer",    "Hydrometer"},
    {ReadingMethod::Refractometer, "refractometer", "Refractometer"},
    {ReadingMethod::Digital,       "digital",       "Digital meter"},  // density meter, floating hydrometer...
};

constexpr EnumText<VesselCondition> kVesselConditions[] = {
    {VesselCondition::Clean,        "clean",          "Clean"},
    {VesselCondition::Dirty,        "dirty",          "Needs cleaning"},
    {VesselCondition::OutOfService, "out_of_service", "Out of service"},
};

constexpr EnumText<PackageFormat> kPackageFormatTexts[] = {
    {PackageFormat::Kegs,          "kegs",         "Kegs"},
    {PackageFormat::CansOrBottles, "cans_bottles", "Cans or bottles"},
};

constexpr EnumText<VolumeReason> kVolumeReasons[] = {
    {VolumeReason::Brewed,      "brewed",       "Brewed"},
    {VolumeReason::TransferIn,  "transfer_in",  "Transferred in"},
    {VolumeReason::TransferOut, "transfer_out", "Transferred out"},
    {VolumeReason::Kegged,      "kegged",       "Kegged"},
    {VolumeReason::Bottled,     "bottled",      "Canned or bottled"},
    {VolumeReason::Loss,        "loss",         "Loss"},
    {VolumeReason::Dumped,      "dumped",       "Dumped"},
    {VolumeReason::Adjusted,    "adjusted",     "Volume corrected"},
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

std::string minutes_text(double minutes) {
    char text[32];
    std::snprintf(text, sizeof text, "%g min", minutes);
    return text;
}

// Which addition of a hop this is, e.g. "Boil, 60 min".
std::string hop_detail(const HopAddition& addition) {
    const std::string use(name(addition.use));
    switch (addition.use) {
    case calc::HopUse::Boil:
    case calc::HopUse::Whirlpool: return use + ", " + minutes_text(addition.time_min);
    default:                      return use;
    }
}

} // namespace

// --- Brewing ----------------------------------------------------------------

std::vector<IngredientUse> planned_ingredients(const Recipe& recipe) {
    std::vector<IngredientUse> uses;
    for (const FermentableAddition& addition : recipe.fermentables) {
        uses.push_back({IngredientKind::Fermentable, addition.library_id, addition.fermentable.name,
                        std::string(name(addition.fermentable.type)), AmountKind::Mass, addition.mass_kg,
                        addition.mass_kg, {}});
    }
    for (const HopAddition& addition : recipe.hops) {
        uses.push_back({IngredientKind::Hop, addition.library_id, addition.hop.name, hop_detail(addition),
                        AmountKind::Mass, addition.mass_kg, addition.mass_kg, {}});
    }
    for (const YeastAddition& addition : recipe.yeasts) {
        IngredientUse use{IngredientKind::Yeast, addition.library_id, addition.yeast.name,
                          std::string(name(addition.yeast.form)), AmountKind::Count, addition.packs, 0.0, {}};
        if (addition.yeast.form == YeastForm::Dry) {
            use.amount_kind = AmountKind::Mass;
            use.planned = addition.mass_kg;
        } else if (addition.yeast.form == YeastForm::Slurry) {
            use.amount_kind = AmountKind::Volume;
            use.planned = addition.slurry_l;
        }
        use.actual = use.planned;
        uses.push_back(std::move(use));
    }
    for (const MiscAddition& addition : recipe.miscs) {
        uses.push_back({IngredientKind::Misc, 0, addition.name, std::string(name(addition.use)), AmountKind::Text,
                        0.0, 0.0, addition.amount});
    }
    return uses;
}

// --- Batches ----------------------------------------------------------------

bool is_active(BatchStatus status) {
    return status == BatchStatus::Brewing || status == BatchStatus::Fermenting ||
           status == BatchStatus::Conditioning;
}

std::optional<double> Batch::og() const {
    double volume_l = 0.0;
    double weighted = 0.0;
    for (const Brew& brew : brews) {
        if (brew.in_progress() || !brew.og || brew.volume_l <= 0.0) continue;
        volume_l += brew.volume_l;
        weighted += *brew.og * brew.volume_l;
    }
    if (volume_l <= 0.0) return std::nullopt;
    return weighted / volume_l;
}

std::optional<Timestamp> Batch::brewed_at() const {
    std::optional<Timestamp> first;
    for (const Brew& brew : brews) {
        if (brew.knocked_out_at && (!first || *brew.knocked_out_at < *first)) first = brew.knocked_out_at;
    }
    return first;
}

const Brew* Batch::brew_in_progress() const {
    for (const Brew& brew : brews) {
        if (brew.in_progress()) return &brew;
    }
    return nullptr;
}

const Reading* Batch::latest_gravity() const {
    const Reading* latest = nullptr;
    for (const Reading& reading : readings) {
        if (reading.gravity_sg && (!latest || reading.at >= latest->at)) latest = &reading;
    }
    return latest;
}

const Reading* Batch::latest_temperature() const {
    const Reading* latest = nullptr;
    for (const Reading& reading : readings) {
        if (reading.temperature_c && (!latest || reading.at >= latest->at)) latest = &reading;
    }
    return latest;
}

std::string batch_label(std::string_view code, std::string_view name) {
    std::string label = code.starts_with('#') ? std::string(code) : "#" + std::string(code);
    if (!name.empty()) {
        label += ' ';
        label += name;
    }
    return label;
}

std::string next_batch_code(std::string_view previous) {
    if (previous.empty()) return "1";

    std::size_t start = previous.size();
    while (start > 0 && previous[start - 1] >= '0' && previous[start - 1] <= '9') --start;
    if (start == previous.size()) return std::string(previous) + "-2";

    // Add one to the digits as text, so long numbers can't overflow and
    // zero padding is kept.
    std::string number(previous.substr(start));
    std::size_t i = number.size();
    while (i > 0 && number[i - 1] == '9') number[--i] = '0';
    if (i == 0) {
        number.insert(number.begin(), '1');
    } else {
        ++number[i - 1];
    }
    return std::string(previous.substr(0, start)) + number;
}

// --- Vessels ----------------------------------------------------------------

bool holds_beer(ElementType type) {
    return type == ElementType::Fermenter || type == ElementType::BriteTank;
}

// --- Names and keys -----------------------------------------------------------

std::string_view name(BatchStatus status) { return lookup(status, kBatchStatuses).name; }
std::string_view name(ReadingMethod method) { return lookup(method, kReadingMethodTexts).name; }
std::string_view name(VesselCondition condition) { return lookup(condition, kVesselConditions).name; }
std::string_view name(PackageFormat format) { return lookup(format, kPackageFormatTexts).name; }
std::string_view name(VolumeReason reason) { return lookup(reason, kVolumeReasons).name; }

std::string_view key(BatchStatus status) { return lookup(status, kBatchStatuses).key; }
std::string_view key(IngredientKind kind) { return lookup(kind, kIngredientKinds).key; }
std::string_view key(AmountKind kind) { return lookup(kind, kAmountKinds).key; }
std::string_view key(ReadingMethod method) { return lookup(method, kReadingMethodTexts).key; }
std::string_view key(VesselCondition condition) { return lookup(condition, kVesselConditions).key; }
std::string_view key(VolumeReason reason) { return lookup(reason, kVolumeReasons).key; }

std::optional<BatchStatus> batch_status_from_key(std::string_view text) { return from_key(text, kBatchStatuses); }
std::optional<IngredientKind> ingredient_kind_from_key(std::string_view text) { return from_key(text, kIngredientKinds); }
std::optional<AmountKind> amount_kind_from_key(std::string_view text) { return from_key(text, kAmountKinds); }
std::optional<ReadingMethod> reading_method_from_key(std::string_view text) { return from_key(text, kReadingMethodTexts); }
std::optional<VesselCondition> vessel_condition_from_key(std::string_view text) { return from_key(text, kVesselConditions); }
std::optional<VolumeReason> volume_reason_from_key(std::string_view text) { return from_key(text, kVolumeReasons); }

} // namespace ninkasi::domain
