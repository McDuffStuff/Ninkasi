#pragma once
// Selling beer: the channels it goes out through, the customers it goes to,
// the price list, the sales themselves, and the kegs and deposits customers
// hold.

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
    Pour,     // beer by the pour, priced by volume: in the taproom, or a growler filled
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
    double yield = 0.9;          // pours: the share of a keg that reaches glasses
    // In the brewery's currency: each unit sold (a keg, a pack); for pours, a
    // liter (shown per fl oz or per 50 mL).
    double price = 0.0;
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
    Date on{};
};

// Whether two names are the same beer: case is ignored, as in packaged goods.
bool same_beer(std::string_view a, std::string_view b);

// Whether two entries price the same thing (beer, channel, and the package
// and its size, or pours): a later one of them replaces an earlier one.
bool same_priced_thing(const PriceEntry& a, const PriceEntry& b);

// The price list entry for `query`: the beer's own price before the price
// for every beer, and of those the one in effect on the query's date (the
// latest to have started by then). nullptr if there's none.
const PriceEntry* find_price(std::span<const PriceEntry> prices, const PriceQuery& query);

// The entries in effect on `date`: for each thing priced, the latest to have
// started by then. In the order given.
std::vector<PriceEntry> prices_in_effect(std::span<const PriceEntry> prices, Date date);

// What a keg brings in sold by the pour: its volume times the pour price's
// yield, at its price per liter.
double keg_pour_value(double keg_l, const PriceEntry& pour);

// --- Sales ----------------------------------------------------------------------

// Where a sale came from.
enum class SaleSource {
    Entered,     // the sale form
    TakeOut,     // packaged goods' Take out, as sold
    Pos,         // imported from the taproom's point of sale
    TaproomKeg,  // a keg used up in the taproom, counted as sold by the pour
    Takings,     // the taproom's takings, entered by hand
    KegReturn,   // deposits refunded on kegs brought back
};
inline constexpr std::array kSaleSources = {SaleSource::Entered, SaleSource::TakeOut,    SaleSource::Pos,
                                            SaleSource::TaproomKeg, SaleSource::Takings, SaleSource::KegReturn};

// Whether a sale from `source` can be deleted as a mistake. The others are
// undone where they were made: a POS import by importing the day again, a
// deposit refund with the kegs it came with.
bool can_delete(SaleSource source);

// What a line of a sale is.
enum class LineKind {
    Package,  // packaged beer: kegs, or cans one at a time or in packs
    Pour,     // beer poured: in the taproom, or a growler filled
    Deposit,  // a keg deposit charged, or refunded (a negative quantity): not revenue
    Other,    // anything else: merchandise, delivery, a day's takings...
};
inline constexpr std::array kLineKinds = {LineKind::Package, LineKind::Pour, LineKind::Deposit, LineKind::Other};

struct SaleLine {
    std::int64_t id = 0;
    LineKind kind = LineKind::Package;
    std::string beer;                               // packages and pours
    PackageType package = PackageType::HalfBarrel;  // packages, and the keg a deposit is on
    double unit_volume_l = 0.0;                     // packages: what each holds
    double quantity = 0.0;  // packs, pours, kegs (deposits: negative when refunded) or other units
    int pack_size = 1;      // packages: how many in each pack
    double pour_l = 0.0;    // pours: how much each is
    std::optional<double> unit_price;  // each, in the brewery's currency; none until priced
    std::string description;           // other lines: what was sold

    bool operator==(const SaleLine&) const = default;

    // Package lines: how many packages left packaged goods. 0 for the rest.
    int packages() const;
    // The beer sold, by its packages or pours. 0 for deposits and other lines.
    double volume_l() const;
    // Quantity times price; 0 until it's priced.
    double amount() const;
};

struct Sale {
    std::int64_t id = 0;
    Timestamp at{};
    Channel channel = Channel::Distributor;
    std::optional<std::int64_t> customer_id;
    std::string number;  // invoice or order number
    SaleSource source = SaleSource::Entered;
    std::optional<std::int64_t> import_id;  // the POS import it came from
    std::string note;
    std::vector<SaleLine> lines;
    std::string customer;  // the customer's name, when read back
    std::string user;      // who recorded it, when read back

    // What it earned: every line but deposits, which are paid back.
    double revenue() const;
    // Keg deposits charged, less those refunded.
    double deposits() const;
    // Whether a line has no price yet.
    bool needs_price() const;
};

// The price list query for a package or pour line of a sale, at the sale's
// channel and on its (local) date. Its beer refers to the line's. nullopt for
// deposits and other lines.
std::optional<PriceQuery> price_query(const Sale& sale, const SaleLine& line);

// The price list's price for a line of a sale (see price_query): a pour's
// is its size at the price per liter. A keg used up in the taproom is priced
// by what it brings in by the pour (see keg_pour_value). nullopt if the
// price list has none.
std::optional<double> list_price(std::span<const PriceEntry> prices, const Sale& sale, const SaleLine& line);

// --- Taproom revenue ---------------------------------------------------------------

// Where the taproom's revenue is counted from. Whichever it is, the others
// would count it twice.
enum class TaproomRevenue {
    KegsUsedUp,   // each keg taken out for the taproom, as the pours it gives
    PointOfSale,  // imported from the point of sale's exports
    Takings,      // each day's takings, entered by hand
};
inline constexpr std::array kTaproomRevenues = {TaproomRevenue::KegsUsedUp, TaproomRevenue::PointOfSale,
                                                TaproomRevenue::Takings};

// --- Point-of-sale exports ------------------------------------------------------------

// One row of a point-of-sale export: an item sold, or the day's total of it.
struct PosRow {
    Date day{};
    std::string item;
    double quantity = 1.0;  // negative for a refund
    double amount = 0.0;    // net of discounts; negative for a refund

    bool operator==(const PosRow&) const = default;
};

// What an item on the point of sale is.
enum class PosItemKind {
    Pour,     // a pour of a beer, or a growler filled
    Package,  // cans sold to go
    Other,    // anything else: food, merchandise...
    Ignore,   // not a sale: tips, gift cards...
};
inline constexpr std::array kPosItemKinds = {PosItemKind::Pour, PosItemKind::Package, PosItemKind::Other,
                                             PosItemKind::Ignore};

// How an item on the point of sale is recorded. Kept, so each item is
// mapped once.
struct PosItem {
    std::string item;  // as the point of sale names it
    PosItemKind kind = PosItemKind::Other;
    std::string beer;                        // pours and packages
    double pour_l = 0.0;                     // pours
    PackageType package = PackageType::Can;  // packages
    double unit_volume_l = 0.0;              // packages
    int pack_size = 1;                       // packages

    bool operator==(const PosItem&) const = default;
};

// The item mapping `item` (by name, ignoring case), or nullptr.
const PosItem* find_pos_item(std::span<const PosItem> items, std::string_view item);

// The sales an export makes.
struct PosSales {
    std::vector<Sale> sales;
    std::vector<std::string> unmapped;  // items with no mapping, left out
    int netted_out = 0;                 // lines whose refunds cancelled their sales
};

// The sales of a point-of-sale export: for each day, one in the taproom
// (pours and other items) and one to go (cans), at the end of the day. Each
// item's rows for a day make one line at their average price. Items left out
// (Ignore) aren't recorded, nor are those a day's refunds cancel.
PosSales pos_sales(std::span<const PosRow> rows, std::span<const PosItem> items);

// --- Keg deposits and kegs at customers -------------------------------------------

// What the brewery charges as a deposit on each keg it sells, by size: 0 for
// no deposit.
struct KegDeposits {
    double half_barrel = 0.0;
    double sixtel = 0.0;

    double of(PackageType type) const;
    bool operator==(const KegDeposits&) const = default;
};

// The deposit lines for the kegs sold in `lines`: one for each size of keg
// that has a deposit.
std::vector<SaleLine> deposit_lines(std::span<const SaleLine> lines, const KegDeposits& deposits);

// One customer's kegs of one size.
struct KegAccount {
    int sold = 0;           // kegs sold to them
    int returned = 0;       // kegs they brought back
    int deposit_kegs = 0;   // kegs they've paid a deposit on and not had it back
    double deposits = 0.0;  // what those deposits came to

    bool operator==(const KegAccount&) const = default;

    // Kegs still at the customer. Never below 0: kegs sold before sales were
    // recorded come back too.
    int out() const;
    // The refund for `count` kegs of `type` brought back: the deposits paid
    // on as many of them as have one, at the average paid. nullopt if none do.
    std::optional<SaleLine> refund(PackageType type, int count) const;
};

// --- Names and stored keys ----------------------------------------------------
// Keys are stored in the database: never change one once shipped.

std::string_view name(Channel channel);  // "Distributor", "Retail accounts", "Taproom", "To-go", "Other"
std::string_view name(PricedAs priced_as);
std::string_view key(Channel channel);
std::string_view key(PricedAs priced_as);
std::optional<Channel> channel_from_key(std::string_view text);
std::optional<PricedAs> priced_as_from_key(std::string_view text);
std::string_view name(SaleSource source);  // "Sale", "Take out", "Point of sale"...
std::string_view key(SaleSource source);
std::string_view key(LineKind kind);
std::optional<SaleSource> sale_source_from_key(std::string_view text);
std::optional<LineKind> line_kind_from_key(std::string_view text);
std::string_view name(TaproomRevenue revenue);  // "Kegs used up in the taproom"...
std::string_view key(TaproomRevenue revenue);
std::optional<TaproomRevenue> taproom_revenue_from_key(std::string_view text);
std::string_view name(PosItemKind kind);  // "A pour of beer", "Cans to go"...
std::string_view key(PosItemKind kind);
std::optional<PosItemKind> pos_item_kind_from_key(std::string_view text);

} // namespace ninkasi::domain
