#pragma once
// Selling beer: the channels it goes out through, the customers it goes to,
// and the price list.

#include "domain/packaging.h"
#include "domain/timestamp.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

// How beer reaches the people who drink it. Each brewery uses whichever
// suit it.
enum class Channel {
    Distributor,  // wholesale, to a distributor
    Retail,       // straight to bars, restaurants and stores (self-distribution)
    Taproom,      // poured at the brewery
    ToGo,         // cans and growlers sold at the brewery to take away
    Other,
};
inline constexpr std::array kChannels = {Channel::Distributor, Channel::Retail, Channel::Taproom, Channel::ToGo,
                                         Channel::Other};

struct Customer {
    std::int64_t id = 0;
    std::string name;
    Channel channel = Channel::Distributor;
    std::string contact;  // phone, email, address...
    std::string notes;
    bool active = true;   // no longer bought from: kept for the sales already made

    bool operator==(const Customer&) const = default;
};

// What a price is for.
enum class PricedAs {
    Package,  // packages: a keg, or cans sold one at a time or in packs
    Pour,     // a pour in the taproom
};
inline constexpr std::array kPricedAs = {PricedAs::Package, PricedAs::Pour};

// One line of the price list. A price applies from its date until a later
// price for the same thing takes over, so a sale keeps the price it had.
struct PriceEntry {
    std::int64_t id = 0;
    std::string beer;  // empty: every beer without a price of its own
    Channel channel = Channel::Distributor;
    PricedAs priced_as = PricedAs::Package;
    PackageType package = PackageType::HalfBarrel;  // packages
    double unit_volume_l = 0.0;  // cans: what one holds, or 0 for any can; unused for kegs
    int pack_size = 1;           // packages in each unit sold: a case of cans is 24
    double pour_l = 0.0;         // pours: how much one is
    double yield = 0.9;          // pours: the share of a keg that's sold as pours
    double price = 0.0;          // each unit sold, a pack or a pour, in the brewery's currency
    Date effective_from{};

    bool operator==(const PriceEntry&) const = default;
};

// What a price is wanted for.
struct PriceQuery {
    std::string_view beer;
    Channel channel = Channel::Distributor;
    PricedAs priced_as = PricedAs::Package;
    PackageType package = PackageType::HalfBarrel;
    double unit_volume_l = 0.0;  // cans
    int pack_size = 1;
    double pour_l = 0.0;  // pours; 0 for whichever pour is largest (a pint, usually)
    Date on{};
};

// Whether two names are the same beer: case is ignored, as in packaged goods.
bool same_beer(std::string_view a, std::string_view b);

// Whether two entries price the same thing (beer, channel, package or pour,
// size): a later one of them replaces an earlier one.
bool same_priced_thing(const PriceEntry& a, const PriceEntry& b);

// The price list entry for `query`: the beer's own price before the price
// for every beer, and of those the one in effect on the query's date (the
// latest to have started by then). nullptr if there's none.
const PriceEntry* find_price(std::span<const PriceEntry> prices, const PriceQuery& query);

// The entries in effect on `date`: for each thing priced, the latest to have
// started by then. In the order given.
std::vector<PriceEntry> prices_in_effect(std::span<const PriceEntry> prices, Date date);

// What a keg brings in sold by the pour: the pours it gives (its volume times
// the pour's yield, divided by the pour size) at the pour's price. 0 if the
// pour has no size.
double keg_pour_value(double keg_l, const PriceEntry& pour);

// --- Names and stored keys ----------------------------------------------------
// Keys are stored in the database: never change one once shipped.

std::string_view name(Channel channel);  // "Distributor", "Retail accounts", "Taproom", "To-go", "Other"
std::string_view name(PricedAs priced_as);
std::string_view key(Channel channel);
std::string_view key(PricedAs priced_as);
std::optional<Channel> channel_from_key(std::string_view text);
std::optional<PricedAs> priced_as_from_key(std::string_view text);

} // namespace ninkasi::domain
