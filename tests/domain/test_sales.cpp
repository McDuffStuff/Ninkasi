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
