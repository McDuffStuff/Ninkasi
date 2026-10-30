#include "app/packaged_goods_service.h"
#include "app/sales_service.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "test_helpers.h"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ninkasi;
using domain::Channel;

namespace {

struct Brewery {
    data::Database db{":memory:"};
    app::Session admin;
    data::SettingsRepository settings{db};
    std::optional<app::PackagedGoodsService> goods;

    Brewery() {
        data::migrate(db);
        admin = app::open_local_session(db);
        goods.emplace(db, admin);
    }

    app::SalesService sales(const app::Session& session) {
        return app::SalesService(db, session, settings, *goods);
    }

    app::Session brewer() {
        app::UserService users(db, app::PasswordCost{1024, 1});
        users.set_password(admin, admin.user_id, "admin");
        return *users.sign_in(users.add(admin, "Sam", domain::Role::Brewer, ""), "");
    }
};

domain::PriceEntry half_barrel(std::string beer, double price) {
    domain::PriceEntry entry;
    entry.beer = std::move(beer);
    entry.channel = Channel::Distributor;
    entry.package = domain::PackageType::HalfBarrel;
    entry.price = price;
    entry.effective_from = {2026, 1, 1};
    return entry;
}

} // namespace

TEST_CASE("customers are added and changed by anyone, and need a name") {
    Brewery brewery;
    app::SalesService sales = brewery.sales(brewery.brewer());
    const std::int64_t id = sales.add_customer({0, "  Hopkins Distributing ", Channel::Distributor, "555-0100", "", true});
    REQUIRE(sales.find_customer(id));
    CHECK(sales.find_customer(id)->name == "Hopkins Distributing");
    CHECK_THROWS_AS(sales.add_customer({0, " ", Channel::Retail, "", "", true}), std::invalid_argument);

    domain::Customer gone = *sales.find_customer(id);
    gone.active = false;
    sales.update_customer(gone);
    app::SalesService again = brewery.sales(brewery.admin);
    REQUIRE(again.customers().size() == 1);
    CHECK_FALSE(again.customers().front().active);
}

TEST_CASE("the price list is kept, and only admins change it") {
    Brewery brewery;
    app::SalesService sales = brewery.sales(brewery.admin);
    sales.add_price(half_barrel("", 140.0));
    const std::int64_t hazy = sales.add_price(half_barrel("Hazy IPA", 160.0));

    domain::PriceQuery query;
    query.beer = "Red Ale";
    query.channel = Channel::Distributor;
    query.on = {2026, 3, 1};
    CHECK(sales.price_for(query)->price == 140.0);
    query.beer = "Hazy IPA";
    CHECK(sales.price_for(query)->price == 160.0);

    domain::PriceEntry changed = half_barrel("Hazy IPA", 165.0);
    changed.id = hazy;
    sales.update_price(changed);
    app::SalesService again = brewery.sales(brewery.admin);
    CHECK(again.price_for(query)->price == 165.0);
    again.remove_price(hazy);
    CHECK(again.price_for(query)->price == 140.0);

    CHECK_THROWS_AS(sales.add_price(half_barrel("", -1.0)), std::invalid_argument);
    domain::PriceEntry pour = half_barrel("", 15.0);
    pour.priced_as = domain::PricedAs::Pour;
    pour.yield = 1.2;
    CHECK_THROWS_AS(sales.add_price(pour), std::invalid_argument);  // over 100 %
    pour.yield = 0.9;
    pour.channel = domain::Channel::Taproom;
    const std::int64_t pour_id = sales.add_price(pour);  // a pour, with no package, is stored
    bool found = false;
    for (const domain::PriceEntry& price : sales.prices()) {
        if (price.id != pour_id) continue;
        found = true;
        CHECK(price.priced_as == domain::PricedAs::Pour);
        CHECK(price.price == doctest::Approx(15.0));
        CHECK(price.yield == doctest::Approx(0.9));
    }
    CHECK(found);

    app::SalesService brewer_sales = brewery.sales(brewery.brewer());
    CHECK_FALSE(brewer_sales.can_set_prices());
    CHECK_THROWS_AS(brewer_sales.add_price(half_barrel("Red Ale", 1.0)), app::NotAllowed);
    CHECK_THROWS_AS(brewer_sales.set_currency({}), app::NotAllowed);
    CHECK(brewer_sales.price_for(query)->price == 140.0);  // everyone reads prices
}

TEST_CASE("the currency is kept with the brewery") {
    Brewery brewery;
    {
        app::SalesService sales = brewery.sales(brewery.admin);
        CHECK(sales.currency() == domain::Currency{});
        sales.set_currency({"€", true, true, 2});
    }
    app::SalesService again = brewery.sales(brewery.admin);
    CHECK(again.currency() == domain::Currency{"€", true, true, 2});
}

namespace {

using domain::PackageType;

domain::Timestamp day(int n) {
    return *domain::parse_iso8601("2026-10-01T12:00:00Z") + std::chrono::hours(24 * n);
}

// A brewery with beer to sell: two batches of Red Ale in half-barrels (10
// and 6) and one in cans (96), packaged on days 1, 2 and 3, and a
// distributor to sell to.
struct Cooler : Brewery {
    double half = domain::keg_volume_l(PackageType::HalfBarrel);
    std::int64_t batch40 = batch("40");
    std::int64_t batch41 = batch("41");
    std::int64_t hopkins = 0;

    Cooler() {
        data::Transaction tx(db);
        goods->write_packaged(batch40, std::nullopt, PackageType::HalfBarrel, half, 10, day(1), "");
        goods->write_packaged(batch41, std::nullopt, PackageType::HalfBarrel, half, 6, day(2), "");
        goods->write_packaged(batch41, std::nullopt, PackageType::Can, 0.473, 96, day(3), "");
        tx.commit();
        goods->refresh();
        hopkins = sales(admin).add_customer({0, "Hopkins Distributing", Channel::Distributor, "", "", true});
    }

    std::int64_t batch(const std::string& code) {
        db.exec("INSERT INTO batches (code, name, recipe, status) VALUES ('" + code +
                "', 'Red Ale', '{}', 'packaged');");
        return db.last_insert_id();
    }

    int on_hand(std::int64_t batch_id, PackageType type) const {
        for (const domain::PackagedStock& entry : goods->packaged()) {
            if (entry.batch_id == batch_id && entry.type == type) return entry.count;
        }
        return 0;
    }

    // Packaged goods rows taken out for a sale line.
    std::vector<data::PackagedMovementRow> taken_for(std::int64_t line_id) {
        std::vector<data::PackagedMovementRow> rows;
        for (const data::PackagedMovementRow& row : goods->packaged_history()) {
            if (row.movement.sale_line_id == line_id) rows.push_back(row);
        }
        return rows;
    }
};

domain::SaleLine kegs(int count) {
    domain::SaleLine line;
    line.beer = "Red Ale";
    line.package = PackageType::HalfBarrel;
    line.unit_volume_l = domain::keg_volume_l(PackageType::HalfBarrel);
    line.quantity = count;
    return line;
}

domain::SaleLine four_packs(int count) {
    domain::SaleLine line;
    line.beer = "Red Ale";
    line.package = PackageType::Can;
    line.unit_volume_l = 0.473;
    line.quantity = count;
    line.pack_size = 4;
    return line;
}

domain::Sale sale_to(std::int64_t customer, std::vector<domain::SaleLine> lines, int on_day) {
    domain::Sale sale;
    sale.at = day(on_day);
    sale.channel = Channel::Distributor;
    sale.customer_id = customer;
    sale.lines = std::move(lines);
    return sale;
}

} // namespace

TEST_CASE("a sale takes its beer out oldest batch first, linked to its lines") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.brewer());
    const std::int64_t id = sales.record_sale(sale_to(cooler.hopkins, {kegs(12), four_packs(3)}, 4), false);

    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 0);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::HalfBarrel) == 4);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::Can) == 84);  // 3 packs of 4

    const std::vector<domain::Sale> recorded = sales.sales();
    REQUIRE(recorded.size() == 1);
    const domain::Sale& sale = recorded.front();
    CHECK(sale.id == id);
    CHECK(sale.customer == "Hopkins Distributing");
    REQUIRE(sale.lines.size() == 2);
    CHECK(sale.needs_price());  // there's no price list yet

    // The kegs came from both batches, each removal linked to the keg line.
    const std::vector<data::PackagedMovementRow> taken = cooler.taken_for(sale.lines[0].id);
    REQUIRE(taken.size() == 2);
    int from40 = 0;
    int from41 = 0;
    for (const data::PackagedMovementRow& row : taken) {
        CHECK(row.movement.reason == domain::PackagedReason::Sold);
        (row.movement.batch_id == cooler.batch40 ? from40 : from41) -= row.movement.quantity;
    }
    CHECK(from40 == 10);
    CHECK(from41 == 2);
    REQUIRE(cooler.taken_for(sale.lines[1].id).size() == 1);
    CHECK(cooler.taken_for(sale.lines[1].id).front().movement.quantity == -12);

    // Kegs sold leave with the beer: they're out at the customer.
    CHECK(sales.keg_account(cooler.hopkins, PackageType::HalfBarrel).out() == 12);
    CHECK(cooler.goods->kegs(PackageType::HalfBarrel).dirty == 0);
}

TEST_CASE("a sale of more than is on hand saves nothing") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.admin);
    // Two lines of the same beer: together they're more than the 16 kegs on hand.
    CHECK_THROWS_WITH_AS(sales.record_sale(sale_to(cooler.hopkins, {kegs(10), kegs(7)}, 4), false),
                         "There are only 6 half-barrels of Red Ale.", std::invalid_argument);
    CHECK(sales.sales().empty());
    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 10);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::HalfBarrel) == 6);

    CHECK_THROWS_AS(sales.record_sale(sale_to(cooler.hopkins, {}, 4), false), std::invalid_argument);
    CHECK_THROWS_AS(sales.record_sale(sale_to(cooler.hopkins, {kegs(0)}, 4), false), std::invalid_argument);
    domain::SaleLine nothing;
    nothing.kind = domain::LineKind::Other;
    nothing.quantity = 1;
    CHECK_THROWS_AS(sales.record_sale(sale_to(cooler.hopkins, {nothing}, 4), false), std::invalid_argument);
}

TEST_CASE("sales are priced from the price list, then or later") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.admin);
    sales.add_price(half_barrel("", 140.0));  // from 2026-01-01

    domain::SaleLine discounted = kegs(1);
    discounted.unit_price = 120.0;
    sales.record_sale(sale_to(cooler.hopkins, {kegs(2), discounted, four_packs(1)}, 4), false);
    domain::Sale sale = sales.sales().front();
    CHECK(sale.lines[0].unit_price == 140.0);
    CHECK(sale.lines[1].unit_price == 120.0);  // the price given is kept
    CHECK_FALSE(sale.lines[2].unit_price);     // no price for cans
    CHECK(sale.revenue() == doctest::Approx(400.0));

    domain::PriceEntry cans;
    cans.channel = Channel::Distributor;
    cans.package = PackageType::Can;
    cans.pack_size = 4;
    cans.price = 12.0;
    cans.effective_from = {2026, 1, 1};
    sales.add_price(cans);
    const app::SalesService::Pricing pricing = sales.price_from_list();
    CHECK(pricing.priced == 1);
    CHECK(pricing.unpriced == 0);
    sale = sales.sales().front();
    CHECK_FALSE(sale.needs_price());
    CHECK(sale.revenue() == doctest::Approx(412.0));

    sales.set_line_price(sale.lines[2].id, 11.0);
    CHECK(sales.sales().front().revenue() == doctest::Approx(411.0));

    app::SalesService brewer_sales = cooler.sales(cooler.brewer());
    CHECK_THROWS_AS(brewer_sales.set_line_price(sale.lines[2].id, 0.0), app::NotAllowed);
    CHECK_THROWS_AS(brewer_sales.price_from_list(), app::NotAllowed);
    CHECK_THROWS_AS(brewer_sales.delete_sale(sale.id), app::NotAllowed);
    CHECK_THROWS_AS(brewer_sales.set_keg_deposits({30.0, 30.0}), app::NotAllowed);
}

TEST_CASE("keg deposits are charged on kegs sold and refunded when they come back") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.admin);
    sales.set_keg_deposits({30.0, 0.0});
    sales.record_sale(sale_to(cooler.hopkins, {kegs(4), four_packs(1)}, 4), true);
    const domain::Sale sale = sales.sales().front();
    REQUIRE(sale.lines.size() == 3);
    CHECK(sale.lines[2].kind == domain::LineKind::Deposit);
    CHECK(sale.deposits() == doctest::Approx(120.0));
    CHECK(sale.revenue() == doctest::Approx(0.0));  // deposits aren't revenue

    domain::KegAccount account = sales.keg_account(cooler.hopkins, PackageType::HalfBarrel);
    CHECK(account.out() == 4);
    CHECK(account.deposits == doctest::Approx(120.0));

    // None on a sale to no one: there'd be no one to refund.
    domain::Sale counter = sale_to(cooler.hopkins, {kegs(1)}, 5);
    counter.customer_id.reset();
    sales.record_sale(counter, true);
    CHECK(sales.sales().front().deposits() == 0.0);

    // Three come back: their deposits are refunded, and they're dirty in the keg cleaner.
    const std::vector<domain::KegCount> back{{PackageType::HalfBarrel, 3}};
    CHECK(sales.return_kegs(cooler.hopkins, back, day(10), "") == doctest::Approx(90.0));
    account = sales.keg_account(cooler.hopkins, PackageType::HalfBarrel);
    CHECK(account.out() == 1);
    CHECK(account.deposit_kegs == 1);
    CHECK(account.deposits == doctest::Approx(30.0));
    CHECK(cooler.goods->kegs(PackageType::HalfBarrel).dirty == 3);
    CHECK(cooler.goods->keg_history().front().customer == "Hopkins Distributing");

    // Kegs sold before sales were recorded come back with no deposit to refund.
    const std::vector<domain::KegCount> more{{PackageType::HalfBarrel, 2}, {PackageType::Sixtel, 1}};
    CHECK(sales.return_kegs(cooler.hopkins, more, day(11), "") == doctest::Approx(30.0));
    CHECK(sales.keg_account(cooler.hopkins, PackageType::HalfBarrel).out() == 0);
    CHECK(sales.keg_account(cooler.hopkins, PackageType::HalfBarrel).deposits == doctest::Approx(0.0));
    CHECK(sales.sales().front().source == domain::SaleSource::KegReturn);
    CHECK_FALSE(sales.sales().front().needs_price());
}

TEST_CASE("deleting a sale puts its beer back") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.admin);
    const std::int64_t id = sales.record_sale(sale_to(cooler.hopkins, {kegs(12)}, 4), true);
    sales.delete_sale(id);
    CHECK(sales.sales().empty());
    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 10);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::HalfBarrel) == 6);
    CHECK(sales.keg_account(cooler.hopkins, PackageType::HalfBarrel).out() == 0);

    // A deposit refund goes with the kegs it came with.
    sales.set_keg_deposits({30.0, 0.0});
    sales.record_sale(sale_to(cooler.hopkins, {kegs(1)}, 5), true);
    const std::vector<domain::KegCount> back{{PackageType::HalfBarrel, 1}};
    sales.return_kegs(cooler.hopkins, back, day(6), "");
    CHECK_THROWS_AS(sales.delete_sale(sales.sales().front().id), std::invalid_argument);
}

TEST_CASE("beer sold in the taproom is removed for the taproom") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.admin);
    domain::Sale sale = sale_to(cooler.hopkins, {four_packs(2)}, 4);
    sale.channel = Channel::Taproom;
    sale.customer_id.reset();
    sales.record_sale(sale, false);
    const domain::SaleLine line = sales.sales().front().lines.front();
    REQUIRE(cooler.taken_for(line.id).size() == 1);
    CHECK(cooler.taken_for(line.id).front().movement.reason == domain::PackagedReason::Taproom);
}

TEST_CASE("kegs used up in the taproom are sold by the pour, when that's where its revenue comes from") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.admin);
    domain::PriceEntry pint;
    pint.channel = Channel::Taproom;
    pint.priced_as = domain::PricedAs::Pour;
    pint.yield = 0.9;
    pint.price = 7.0 / 0.473;  // $7 a pint, per liter
    pint.effective_from = {2026, 1, 1};
    sales.add_price(pint);

    sales.take_to_taproom({.beer = "Red Ale", .type = PackageType::HalfBarrel, .unit_volume_l = cooler.half,
                           .count = 2, .at = day(4)});
    const domain::Sale sale = sales.sales().front();
    CHECK(sale.source == domain::SaleSource::TaproomKeg);
    CHECK(sale.channel == Channel::Taproom);
    CHECK(sale.revenue() == doctest::Approx(2 * cooler.half * 0.9 * 7.0 / 0.473));
    REQUIRE(cooler.taken_for(sale.lines[0].id).size() == 1);
    CHECK(cooler.taken_for(sale.lines[0].id).front().movement.reason == domain::PackagedReason::Taproom);
    CHECK(cooler.goods->kegs(PackageType::HalfBarrel).dirty == 2);  // emptied, back to be cleaned

    // Counted at the point of sale instead: the kegs only leave packaged goods.
    sales.set_taproom_revenue(domain::TaproomRevenue::PointOfSale);
    sales.take_to_taproom({.beer = "Red Ale", .type = PackageType::HalfBarrel, .unit_volume_l = cooler.half,
                           .count = 1, .at = day(5)});
    CHECK(sales.sales().size() == 1);
    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 7);
    CHECK(cooler.sales(cooler.admin).taproom_revenue() == domain::TaproomRevenue::PointOfSale);
    CHECK_THROWS_AS(cooler.sales(cooler.brewer()).set_taproom_revenue(domain::TaproomRevenue::KegsUsedUp),
                    app::NotAllowed);
}

TEST_CASE("a point-of-sale export records each day's sales, and replaces days imported again") {
    Cooler cooler;
    app::SalesService sales = cooler.sales(cooler.brewer());
    sales.save_pos_item({"Red Ale Pint", domain::PosItemKind::Pour, "Red Ale", 0.473});
    sales.save_pos_item({"Red Ale 4-pack", domain::PosItemKind::Package, "Red Ale", 0.0, PackageType::Can, 0.473, 4});
    sales.save_pos_item({"Tip", domain::PosItemKind::Ignore});
    const std::vector<domain::PosRow> rows{
        {{2026, 10, 27}, "red ale pint", 2, 14.0},
        {{2026, 10, 27}, "Red Ale 4-pack", 1, 16.0},
        {{2026, 10, 27}, "Tip", 1, 5.0},
        {{2026, 10, 28}, "Red Ale Pint", 3, 21.0},
        {{2026, 10, 28}, "T-shirt", 1, 25.0},
    };
    CHECK_THROWS_AS(sales.import_pos("export.csv", rows, true, false), std::invalid_argument);  // the T-shirt
    sales.save_pos_item({"T-shirt", domain::PosItemKind::Other});
    CHECK(sales.pos_days_imported(rows) == 0);
    const app::SalesService::PosImport imported = sales.import_pos("export.csv", rows, true, false);
    CHECK(imported.days == 2);
    CHECK(imported.sales == 3);  // the 27th in the taproom and to go, the 28th in the taproom
    CHECK(imported.revenue == doctest::Approx(14.0 + 16.0 + 21.0 + 25.0));
    CHECK(cooler.on_hand(cooler.batch41, PackageType::Can) == 92);

    // Imported again: refused, unless the days are replaced.
    CHECK(sales.pos_days_imported(rows) == 2);
    CHECK_THROWS_AS(sales.import_pos("export.csv", rows, true, false), std::invalid_argument);
    const app::SalesService::PosImport again = sales.import_pos("export.csv", rows, true, true);
    CHECK(again.replaced_days == 2);
    CHECK(sales.sales().size() == 3);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::Can) == 92);  // not taken twice

    // Cans already taken out for the taproom aren't taken again.
    const std::vector<domain::PosRow> next_day{{{2026, 10, 29}, "Red Ale 4-pack", 2, 32.0}};
    sales.import_pos("export.csv", next_day, false, false);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::Can) == 92);
    CHECK_FALSE(sales.pos_takes_cans());
}
