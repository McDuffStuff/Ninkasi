#include "domain/sales.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ninkasi::domain {

namespace {

// Volumes within half a milliliter are the same size.
constexpr double kSameVolume = 0.0005;

bool same_volume(double a, double b) {
    return std::abs(a - b) < kSameVolume;
}

// Whether `entry` prices what `query` asks about, whatever its beer and date.
bool prices(const PriceEntry& entry, const PriceQuery& query) {
    if (entry.channel != query.channel || entry.priced_as != query.priced_as) return false;
    if (entry.priced_as == PricedAs::Pour) return query.pour_l <= 0.0 || same_volume(entry.pour_l, query.pour_l);
    if (entry.package != query.package || entry.pack_size != query.pack_size) return false;
    // A can price is for one size of can, or for any can.
    return entry.package != PackageType::Can || entry.unit_volume_l <= 0.0 ||
           same_volume(entry.unit_volume_l, query.unit_volume_l);
}

struct ChannelText {
    Channel channel;
    std::string_view key;
    std::string_view name;
};

constexpr ChannelText kChannelTexts[] = {
    {Channel::Distributor, "distributor", "Distributor"},
    {Channel::Retail, "retail", "Retail accounts"},
    {Channel::Taproom, "taproom", "Taproom"},
    {Channel::ToGo, "to_go", "To-go"},
    {Channel::Other, "other", "Other"},
};

struct SourceText {
    SaleSource source;
    std::string_view key;
    std::string_view name;
};

constexpr SourceText kSourceTexts[] = {
    {SaleSource::Entered, "entered", "Sale"},
    {SaleSource::TakeOut, "take_out", "Take out"},
    {SaleSource::Pos, "pos", "Point of sale"},
    {SaleSource::TaproomKeg, "taproom_keg", "Taproom keg"},
    {SaleSource::Takings, "takings", "Takings"},
    {SaleSource::KegReturn, "keg_return", "Kegs returned"},
};

constexpr std::string_view kLineKindKeys[] = {"package", "pour", "deposit", "other"};  // by LineKind

} // namespace

bool same_beer(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

bool same_priced_thing(const PriceEntry& a, const PriceEntry& b) {
    if (!same_beer(a.beer, b.beer) || a.channel != b.channel || a.priced_as != b.priced_as) return false;
    if (a.priced_as == PricedAs::Pour) return same_volume(a.pour_l, b.pour_l);
    return a.package == b.package && a.pack_size == b.pack_size &&
           (a.package != PackageType::Can || same_volume(a.unit_volume_l, b.unit_volume_l));
}

const PriceEntry* find_price(std::span<const PriceEntry> prices_list, const PriceQuery& query) {
    // The beer's own prices if it has any in effect, otherwise every beer's.
    for (const bool own : {true, false}) {
        const PriceEntry* best = nullptr;
        for (const PriceEntry& entry : prices_list) {
            if (entry.effective_from > query.on || !prices(entry, query)) continue;
            if (own ? !same_beer(entry.beer, query.beer) || entry.beer.empty() : !entry.beer.empty()) continue;
            if (!best) {
                best = &entry;
                continue;
            }
            // Of pours of any size, the largest; otherwise the latest to start
            // (and of two starting the same day, the one entered last).
            if (query.priced_as == PricedAs::Pour && query.pour_l <= 0.0 && !same_volume(entry.pour_l, best->pour_l)) {
                if (entry.pour_l > best->pour_l) best = &entry;
                continue;
            }
            if (entry.effective_from > best->effective_from ||
                (entry.effective_from == best->effective_from && entry.id > best->id)) {
                best = &entry;
            }
        }
        if (best) return best;
    }
    return nullptr;
}

std::vector<PriceEntry> prices_in_effect(std::span<const PriceEntry> prices_list, Date date) {
    std::vector<PriceEntry> current;
    for (const PriceEntry& entry : prices_list) {
        if (entry.effective_from > date) continue;
        const bool replaced = std::any_of(prices_list.begin(), prices_list.end(), [&](const PriceEntry& other) {
            return &other != &entry && same_priced_thing(other, entry) && other.effective_from <= date &&
                   (other.effective_from > entry.effective_from ||
                    (other.effective_from == entry.effective_from && other.id > entry.id));
        });
        if (!replaced) current.push_back(entry);
    }
    return current;
}

double keg_pour_value(double keg_l, const PriceEntry& pour) {
    if (!(pour.pour_l > 0.0)) return 0.0;
    return keg_l * pour.yield / pour.pour_l * pour.price;
}

// --- Sales ----------------------------------------------------------------------

bool can_delete(SaleSource source) {
    return source == SaleSource::Entered || source == SaleSource::TakeOut || source == SaleSource::Takings;
}

int SaleLine::packages() const {
    if (kind != LineKind::Package) return 0;
    return static_cast<int>(std::lround(quantity)) * pack_size;
}

double SaleLine::volume_l() const {
    switch (kind) {
    case LineKind::Package: return packages() * unit_volume_l;
    case LineKind::Pour:    return quantity * pour_l;
    default:                return 0.0;
    }
}

double SaleLine::amount() const {
    return unit_price ? quantity * *unit_price : 0.0;
}

double Sale::revenue() const {
    double total = 0.0;
    for (const SaleLine& line : lines) {
        if (line.kind != LineKind::Deposit) total += line.amount();
    }
    return total;
}

double Sale::deposits() const {
    double total = 0.0;
    for (const SaleLine& line : lines) {
        if (line.kind == LineKind::Deposit) total += line.amount();
    }
    return total;
}

bool Sale::needs_price() const {
    return std::any_of(lines.begin(), lines.end(), [](const SaleLine& line) { return !line.unit_price; });
}

std::optional<PriceQuery> price_query(const Sale& sale, const SaleLine& line) {
    if (line.kind != LineKind::Package && line.kind != LineKind::Pour) return std::nullopt;
    PriceQuery query;
    query.beer = line.beer;
    query.channel = sale.channel;
    query.on = local_date(sale.at);
    if (line.kind == LineKind::Pour) {
        query.priced_as = PricedAs::Pour;
        query.pour_l = line.pour_l;
    } else {
        query.priced_as = PricedAs::Package;
        query.package = line.package;
        query.unit_volume_l = line.unit_volume_l;
        query.pack_size = line.pack_size;
    }
    return query;
}

// --- Keg deposits and kegs at customers -------------------------------------------

double KegDeposits::of(PackageType type) const {
    switch (type) {
    case PackageType::HalfBarrel: return half_barrel;
    case PackageType::Sixtel:     return sixtel;
    default:                      return 0.0;
    }
}

std::vector<SaleLine> deposit_lines(std::span<const SaleLine> lines, const KegDeposits& deposits) {
    std::vector<SaleLine> charged;
    for (PackageType type : kKegTypes) {
        int kegs = 0;
        for (const SaleLine& line : lines) {
            if (line.kind == LineKind::Package && line.package == type) kegs += line.packages();
        }
        if (kegs <= 0 || !(deposits.of(type) > 0.0)) continue;
        SaleLine deposit;
        deposit.kind = LineKind::Deposit;
        deposit.package = type;
        deposit.quantity = kegs;
        deposit.unit_price = deposits.of(type);
        charged.push_back(deposit);
    }
    return charged;
}

int KegAccount::out() const {
    return std::max(sold - returned, 0);
}

std::optional<SaleLine> KegAccount::refund(PackageType type, int count) const {
    const int kegs = std::min(count, deposit_kegs);
    if (kegs <= 0) return std::nullopt;
    SaleLine line;
    line.kind = LineKind::Deposit;
    line.package = type;
    line.quantity = -kegs;
    line.unit_price = deposits / deposit_kegs;
    return line;
}

std::string_view name(Channel channel) {
    for (const ChannelText& text : kChannelTexts) {
        if (text.channel == channel) return text.name;
    }
    return "Other";
}

std::string_view key(Channel channel) {
    for (const ChannelText& text : kChannelTexts) {
        if (text.channel == channel) return text.key;
    }
    return "other";
}

std::optional<Channel> channel_from_key(std::string_view text) {
    for (const ChannelText& entry : kChannelTexts) {
        if (entry.key == text) return entry.channel;
    }
    return std::nullopt;
}

std::string_view name(PricedAs priced_as) {
    return priced_as == PricedAs::Pour ? "Pour" : "Package";
}

std::string_view key(PricedAs priced_as) {
    return priced_as == PricedAs::Pour ? "pour" : "package";
}

std::optional<PricedAs> priced_as_from_key(std::string_view text) {
    if (text == "pour") return PricedAs::Pour;
    if (text == "package") return PricedAs::Package;
    return std::nullopt;
}

std::string_view name(SaleSource source) {
    for (const SourceText& text : kSourceTexts) {
        if (text.source == source) return text.name;
    }
    return "Sale";
}

std::string_view key(SaleSource source) {
    for (const SourceText& text : kSourceTexts) {
        if (text.source == source) return text.key;
    }
    return "entered";
}

std::optional<SaleSource> sale_source_from_key(std::string_view text) {
    for (const SourceText& entry : kSourceTexts) {
        if (entry.key == text) return entry.source;
    }
    return std::nullopt;
}

std::string_view key(LineKind kind) {
    return kLineKindKeys[static_cast<std::size_t>(kind)];
}

std::optional<LineKind> line_kind_from_key(std::string_view text) {
    for (LineKind kind : kLineKinds) {
        if (key(kind) == text) return kind;
    }
    return std::nullopt;
}

} // namespace ninkasi::domain
