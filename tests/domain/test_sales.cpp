#include "domain/sales.h"
#include "test_helpers.h"

#include <vector>

using namespace ninkasi;
using domain::Channel;
using domain::Date;
using domain::PackageType;
using domain::PriceEntry;
using domain::PricedAs;
using domain::PriceQuery;

namespace {

PriceEntry keg_price(std::string beer, Channel channel, double price, Date from, std::int64_t id) {
    PriceEntry entry;
    entry.id = id;
    entry.beer = std::move(beer);
    entry.channel = channel;
    entry.package = PackageType::HalfBarrel;
    entry.price = price;
    entry.effective_from = from;
    return entry;
}

PriceEntry pour_price(std::string beer, double pour_l, double price, Date from, std::int64_t id) {
    PriceEntry entry;
    entry.id = id;
    entry.beer = std::move(beer);
    entry.channel = Channel::Taproom;
    entry.priced_as = PricedAs::Pour;
    entry.pour_l = pour_l;
    entry.yield = 0.9;
    entry.price = price;
    entry.effective_from = from;
    return entry;
}

PriceQuery half_barrel(std::string_view beer, Channel channel, Date on) {
    PriceQuery query;
    query.beer = beer;
    query.channel = channel;
    query.package = PackageType::HalfBarrel;
    query.on = on;
    return query;
}

} // namespace

TEST_CASE("a beer's own price comes before the price for every beer") {
    const std::vector<PriceEntry> prices = {
        keg_price("", Channel::Distributor, 140.0, {2026, 1, 1}, 1),
        keg_price("Hazy IPA", Channel::Distributor, 160.0, {2026, 1, 1}, 2),
    };
    CHECK(domain::find_price(prices, half_barrel("hazy ipa", Channel::Distributor, {2026, 6, 1}))->price == 160.0);
    CHECK(domain::find_price(prices, half_barrel("Red Ale", Channel::Distributor, {2026, 6, 1}))->price == 140.0);
    // Another channel has no price.
    CHECK_FALSE(domain::find_price(prices, half_barrel("Red Ale", Channel::Retail, {2026, 6, 1})));
}

TEST_CASE("a price applies from its date until a later one takes over") {
    const std::vector<PriceEntry> prices = {
        keg_price("Red Ale", Channel::Distributor, 130.0, {2026, 1, 1}, 1),
        keg_price("Red Ale", Channel::Distributor, 145.0, {2026, 7, 1}, 2),
    };
    CHECK_FALSE(domain::find_price(prices, half_barrel("Red Ale", Channel::Distributor, {2025, 12, 31})));
    CHECK(domain::find_price(prices, half_barrel("Red Ale", Channel::Distributor, {2026, 6, 30}))->price == 130.0);
    CHECK(domain::find_price(prices, half_barrel("Red Ale", Channel::Distributor, {2026, 7, 1}))->price == 145.0);

    const std::vector<PriceEntry> current = domain::prices_in_effect(prices, {2026, 8, 1});
    REQUIRE(current.size() == 1);
    CHECK(current.front().price == 145.0);
    CHECK(domain::prices_in_effect(prices, {2026, 3, 1}).front().price == 130.0);
}

TEST_CASE("can prices are for a can size and a pack size") {
    PriceEntry case_of_16oz;
    case_of_16oz.channel = Channel::Distributor;
    case_of_16oz.package = PackageType::Can;
    case_of_16oz.unit_volume_l = 0.4732;
    case_of_16oz.pack_size = 24;
    case_of_16oz.price = 48.0;
    PriceEntry any_four_pack = case_of_16oz;
    any_four_pack.id = 2;
    any_four_pack.channel = Channel::ToGo;
    any_four_pack.unit_volume_l = 0.0;  // any can
    any_four_pack.pack_size = 4;
    any_four_pack.price = 16.0;
    const std::vector<PriceEntry> prices = {case_of_16oz, any_four_pack};

    PriceQuery query;
    query.beer = "Red Ale";
    query.channel = Channel::Distributor;
    query.package = PackageType::Can;
    query.unit_volume_l = 0.47318;  // within half a milliliter
    query.pack_size = 24;
    query.on = {2026, 1, 1};
    CHECK(domain::find_price(prices, query)->price == 48.0);
    query.unit_volume_l = 0.355;  // 12 oz: no case price for those
    CHECK_FALSE(domain::find_price(prices, query));
    query.pack_size = 1;
    CHECK_FALSE(domain::find_price(prices, query));

    query.channel = Channel::ToGo;
    query.pack_size = 4;
    CHECK(domain::find_price(prices, query)->price == 16.0);  // any can
}

TEST_CASE("pours are priced by size, and a keg is worth its pours") {
    const std::vector<PriceEntry> prices = {
        pour_price("Red Ale", 0.473, 7.0, {2026, 1, 1}, 1),
        pour_price("Red Ale", 0.296, 5.0, {2026, 1, 1}, 2),
    };
    PriceQuery query;
    query.beer = "Red Ale";
    query.channel = Channel::Taproom;
    query.priced_as = PricedAs::Pour;
    query.on = {2026, 2, 1};
    // Any pour: the largest, a pint.
    const PriceEntry* pint = domain::find_price(prices, query);
    REQUIRE(pint);
    CHECK(pint->price == 7.0);
    query.pour_l = 0.296;
    CHECK(domain::find_price(prices, query)->price == 5.0);

    // A half-barrel (58.67 L) at 90 % yield in pints: 111.6 pours at $7.
    CHECK(domain::keg_pour_value(58.67, *pint) == near(58.67 * 0.9 / 0.473 * 7.0, 1e-9));
    PriceEntry no_size = *pint;
    no_size.pour_l = 0.0;
    CHECK(domain::keg_pour_value(58.67, no_size) == 0.0);
}

TEST_CASE("the same priced thing, and the same beer") {
    CHECK(domain::same_beer("Red Ale", "red ale"));
    CHECK_FALSE(domain::same_beer("Red Ale", "Red Ales"));

    PriceEntry a = keg_price("Red Ale", Channel::Distributor, 1.0, {2026, 1, 1}, 1);
    PriceEntry b = keg_price("RED ALE", Channel::Distributor, 2.0, {2026, 2, 1}, 2);
    CHECK(domain::same_priced_thing(a, b));
    b.package = PackageType::Sixtel;
    CHECK_FALSE(domain::same_priced_thing(a, b));
}

TEST_CASE("channel keys round-trip") {
    for (Channel channel : domain::kChannels) CHECK(domain::channel_from_key(domain::key(channel)) == channel);
    CHECK_FALSE(domain::channel_from_key("nowhere"));
}

TEST_CASE("sale lines count packages, volume and money") {
    domain::SaleLine cans;
    cans.beer = "Red Ale";
    cans.package = domain::PackageType::Can;
    cans.unit_volume_l = 0.355;
    cans.quantity = 3;
    cans.pack_size = 6;
    CHECK(cans.packages() == 18);
    CHECK(cans.volume_l() == doctest::Approx(18 * 0.355));
    CHECK(cans.amount() == 0.0);  // not priced yet
    cans.unit_price = 10.0;
    CHECK(cans.amount() == doctest::Approx(30.0));

    domain::SaleLine pours;
    pours.kind = domain::LineKind::Pour;
    pours.beer = "Red Ale";
    pours.quantity = 100;
    pours.pour_l = 0.473;
    pours.unit_price = 7.0;
    CHECK(pours.packages() == 0);
    CHECK(pours.volume_l() == doctest::Approx(47.3));

    domain::SaleLine deposit;
    deposit.kind = domain::LineKind::Deposit;
    deposit.quantity = 2;
    deposit.unit_price = 30.0;
    CHECK(deposit.volume_l() == 0.0);

    domain::Sale sale;
    sale.lines = {cans, pours, deposit};
    CHECK(sale.revenue() == doctest::Approx(730.0));  // deposits aren't earned
    CHECK(sale.deposits() == doctest::Approx(60.0));
    CHECK_FALSE(sale.needs_price());
    sale.lines.push_back({});
    CHECK(sale.needs_price());
}

TEST_CASE("a sale line is priced by its channel, package and date") {
    domain::Sale sale;
    sale.channel = Channel::ToGo;
    sale.at = *domain::parse_iso8601("2026-06-15T18:00:00Z");
    domain::SaleLine cans;
    cans.beer = "Red Ale";
    cans.package = domain::PackageType::Can;
    cans.unit_volume_l = 0.473;
    cans.quantity = 2;
    cans.pack_size = 4;
    const std::optional<domain::PriceQuery> query = domain::price_query(sale, cans);
    REQUIRE(query);
    CHECK(query->beer == "Red Ale");
    CHECK(query->channel == Channel::ToGo);
    CHECK(query->priced_as == domain::PricedAs::Package);
    CHECK(query->pack_size == 4);
    CHECK(query->unit_volume_l == 0.473);
    CHECK(query->on == domain::local_date(sale.at));

    domain::SaleLine other;
    other.kind = domain::LineKind::Other;
    CHECK_FALSE(domain::price_query(sale, other));
}

TEST_CASE("deposits are charged on the kegs of a sale and refunded at what was paid") {
    domain::SaleLine halves;
    halves.package = domain::PackageType::HalfBarrel;
    halves.quantity = 3;
    domain::SaleLine more_halves = halves;
    more_halves.quantity = 2;
    domain::SaleLine sixtels;
    sixtels.package = domain::PackageType::Sixtel;
    sixtels.quantity = 4;
    domain::SaleLine cans;
    cans.package = domain::PackageType::Can;
    cans.quantity = 24;
    const std::vector<domain::SaleLine> lines{halves, more_halves, sixtels, cans};

    const std::vector<domain::SaleLine> deposits = domain::deposit_lines(lines, {30.0, 0.0});
    REQUIRE(deposits.size() == 1);  // sixtels have no deposit
    CHECK(deposits[0].kind == domain::LineKind::Deposit);
    CHECK(deposits[0].package == domain::PackageType::HalfBarrel);
    CHECK(deposits[0].quantity == 5);
    CHECK(deposits[0].unit_price == 30.0);

    domain::KegAccount account{.sold = 5, .returned = 1, .deposit_kegs = 4, .deposits = 110.0};
    CHECK(account.out() == 4);
    const std::optional<domain::SaleLine> refund = account.refund(domain::PackageType::HalfBarrel, 6);
    REQUIRE(refund);
    CHECK(refund->quantity == -4);  // only 4 have a deposit
    CHECK(*refund->unit_price == doctest::Approx(27.5));
    CHECK_FALSE(domain::KegAccount{.sold = 2}.refund(domain::PackageType::HalfBarrel, 1));
    CHECK(domain::KegAccount{.sold = 1, .returned = 3}.out() == 0);
}

TEST_CASE("sale sources and line kinds round-trip through their keys") {
    for (domain::SaleSource source : domain::kSaleSources) {
        CHECK(domain::sale_source_from_key(domain::key(source)) == source);
    }
    for (domain::LineKind kind : domain::kLineKinds) {
        CHECK(domain::line_kind_from_key(domain::key(kind)) == kind);
    }
    CHECK(domain::can_delete(domain::SaleSource::Entered));
    CHECK_FALSE(domain::can_delete(domain::SaleSource::Pos));
}

TEST_CASE("a keg used up in the taproom is priced by the pours it gives") {
    PriceEntry pint;
    pint.id = 1;
    pint.channel = Channel::Taproom;
    pint.priced_as = PricedAs::Pour;
    pint.pour_l = 0.473;
    pint.yield = 0.9;
    pint.price = 7.0;
    pint.effective_from = {2026, 1, 1};
    PriceEntry half_pint = pint;
    half_pint.id = 2;
    half_pint.pour_l = 0.237;
    half_pint.price = 4.0;
    const std::vector<PriceEntry> prices{half_pint, pint};

    domain::Sale sale;
    sale.channel = Channel::Taproom;
    sale.source = domain::SaleSource::TaproomKeg;
    sale.at = *domain::parse_iso8601("2026-06-01T18:00:00Z");
    domain::SaleLine keg;
    keg.beer = "Red Ale";
    keg.package = PackageType::HalfBarrel;
    keg.quantity = 1;
    // By the largest pour.
    CHECK(*domain::list_price(prices, sale, keg) ==
          doctest::Approx(domain::keg_volume_l(PackageType::HalfBarrel) * 0.9 / 0.473 * 7.0));
    sale.source = domain::SaleSource::Entered;
    CHECK_FALSE(domain::list_price(prices, sale, keg));  // no price for a keg sold whole
}

TEST_CASE("a point-of-sale export makes a sale in the taproom, and one to go, each day") {
    const std::vector<domain::PosItem> items{
        {"Pint", domain::PosItemKind::Pour, "Red Ale", 0.473},
        {"4-pack", domain::PosItemKind::Package, "Red Ale", 0.0, PackageType::Can, 0.473, 4},
        {"Tip", domain::PosItemKind::Ignore},
    };
    const std::vector<domain::PosRow> rows{
        {{2026, 10, 27}, "pint", 2, 14.0},
        {{2026, 10, 27}, "Pint", 1, 6.0},  // a discount
        {{2026, 10, 27}, "4-pack", 2, 32.0},
        {{2026, 10, 27}, "4-pack", -2, -32.0},  // refunded
        {{2026, 10, 27}, "Tip", 1, 5.0},
        {{2026, 10, 28}, "Pint", 1, 7.0},
        {{2026, 10, 28}, "4-pack", 1, 16.0},
        {{2026, 10, 28}, "Nachos", 1, 9.0},
    };
    const domain::PosSales made = domain::pos_sales(rows, items);
    CHECK(made.unmapped == std::vector<std::string>{"Nachos"});
    CHECK(made.netted_out == 1);
    REQUIRE(made.sales.size() == 3);

    const domain::Sale& first = made.sales[0];
    CHECK(first.channel == Channel::Taproom);
    CHECK(first.source == domain::SaleSource::Pos);
    CHECK(domain::local_date(first.at) == Date{2026, 10, 27});
    REQUIRE(first.lines.size() == 1);
    CHECK(first.lines[0].kind == domain::LineKind::Pour);
    CHECK(first.lines[0].quantity == 3);
    CHECK(*first.lines[0].unit_price == doctest::Approx(20.0 / 3));
    CHECK(first.lines[0].pour_l == 0.473);

    CHECK(made.sales[1].channel == Channel::Taproom);
    const domain::Sale& to_go = made.sales[2];
    CHECK(to_go.channel == Channel::ToGo);
    REQUIRE(to_go.lines.size() == 1);
    CHECK(to_go.lines[0].packages() == 4);
    CHECK(to_go.revenue() == doctest::Approx(16.0));
}
