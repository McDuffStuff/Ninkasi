#include "app/costing_service.h"
#include "app/expense_service.h"
#include "app/insights_service.h"
#include "app/inventory_service.h"
#include "app/packaged_goods_service.h"
#include "app/sales_service.h"
#include "app/session.h"
#include "app/ttb_service.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/inventory_repository.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "test_helpers.h"

#include <chrono>
#include <optional>
#include <string>

using namespace ninkasi;
using domain::Channel;
using domain::PackageType;

namespace {

domain::Timestamp day(int n) {
    return domain::start_of({2026, 9, 1}) + std::chrono::hours(24 * n + 12);
}

std::string iso(domain::Timestamp at) {
    return domain::to_iso8601(at);
}

// A brewery's September: 100 kg of malt bought for $200; batch 7 of Red Ale
// brewed with 50 kg of it on the 1st (520 L), packaged on the 4th as 10 kegs
// of 50 L. Three kegs sold to a distributor at $150 on the 5th, two used up
// in the taproom on the 6th (pints at $7, 90 % yield), rent of $1,000 a month.
struct Brewery {
    data::Database db{":memory:"};
    app::Session admin;
    data::SettingsRepository settings{db};
    std::optional<app::InventoryService> inventory;
    std::optional<app::PackagedGoodsService> goods;
    std::optional<app::SalesService> sales;
    std::optional<app::ExpenseService> expenses;
    std::optional<app::TtbService> ttb;
    std::optional<app::CostingService> costing;
    std::int64_t batch = 0;
    std::uint64_t changes = 0;

    Brewery() {
        data::migrate(db);
        admin = app::open_local_session(db);
        inventory.emplace(db, admin);
        goods.emplace(db, admin);
        sales.emplace(db, admin, settings, *goods);
        expenses.emplace(db, admin);
        ttb.emplace(db, admin, settings);
        costing.emplace(db, admin);

        domain::InventoryItem malt;
        malt.name = "Pale malt";
        const std::int64_t item = inventory->add_item(malt);
        domain::Invoice invoice;
        invoice.received_at = day(0);
        invoice.lines.push_back({item, 100.0, 200.0, "", std::nullopt});
        inventory->receive(invoice);

        db.exec("INSERT INTO batches (code, name, recipe, status, finished_at) VALUES ('7', 'Red Ale', '{}', "
                "'packaged', '" + iso(day(3)) + "');");
        batch = db.last_insert_id();
        db.exec("INSERT INTO brews (batch_id, started_at, knocked_out_at, volume_l, user_id) VALUES (" +
                std::to_string(batch) + ", '" + iso(day(0)) + "', '" + iso(day(0)) + "', 520, " +
                std::to_string(admin.user_id) + ");");
        data::InventoryRepository stock(db);
        const std::int64_t lot = stock.lots(item).front().lot.id;
        data::Transaction tx(db);
        stock.insert_movement({.at = day(0), .item_id = item, .lot_id = lot, .quantity = -50.0,
                               .reason = domain::StockReason::Brewed, .batch_id = batch},
                              std::nullopt, admin.user_id);
        goods->write_packaged(batch, std::nullopt, PackageType::HalfBarrel, 50.0, 10, day(3), "");
        tx.commit();
        goods->refresh();

        domain::PriceEntry pint;
        pint.channel = Channel::Taproom;
        pint.priced_as = domain::PricedAs::Pour;
        pint.yield = 0.9;
        pint.price = 7.0 / 0.473;  // $7 a pint, per liter
        pint.effective_from = {2026, 1, 1};
        sales->add_price(pint);

        domain::Sale sale;
        sale.at = day(4);
        sale.channel = Channel::Distributor;
        domain::SaleLine kegs;
        kegs.beer = "Red Ale";
        kegs.package = PackageType::HalfBarrel;
        kegs.unit_volume_l = 50.0;
        kegs.quantity = 3;
        kegs.unit_price = 150.0;
        sale.lines.push_back(kegs);
        sales->record_sale(sale, false);
        sales->take_to_taproom({.beer = "Red Ale", .type = PackageType::HalfBarrel, .unit_volume_l = 50.0,
                                .count = 2, .at = day(5)});

        const std::int64_t rent = expenses->categories().front().id;
        expenses->add_recurring({0, rent, "Rent", 1000.0, {2026, 9}, std::nullopt});
    }

    app::InsightsService insights(const app::Session& session) {
        return app::InsightsService(db, session, settings, *ttb, *expenses, *costing, [this] { return ++changes; });
    }
};

const app::InsightsQuery kSeptember{{{2026, 9, 1}, {2026, 9, 30}}, domain::Grouping::Week,
                                    domain::Comparison::PreviousPeriod};

const double kTaproomRevenue = 2 * 50.0 * 0.9 / 0.473 * 7.0;

} // namespace

TEST_CASE("the overview and sales count beer sold and what it brought in") {
    Brewery brewery;
    app::InsightsService insights = brewery.insights(brewery.admin);
    insights.refresh();

    const app::Overview overview = insights.overview(kSeptember);
    CHECK(overview.sold_l.now == doctest::Approx(250.0));
    CHECK(*overview.sold_l.before == 0.0);
    CHECK(overview.brewed_l.now == doctest::Approx(520.0));
    REQUIRE(overview.revenue);
    CHECK(overview.revenue->now == doctest::Approx(450.0 + kTaproomRevenue));
    // $100 of malt over 500 L: $10 a keg, five kegs sold.
    CHECK(overview.gross_margin->now == doctest::Approx((450.0 + kTaproomRevenue - 50.0) / (450.0 + kTaproomRevenue)));
    CHECK(*overview.inventory_value == doctest::Approx(100.0 + 50.0));  // 50 kg of malt, 5 kegs

    const app::SalesInsights sales = insights.sales(kSeptember);
    REQUIRE(sales.channels.size() == 2);
    CHECK(sales.channels[0].channel == Channel::Distributor);
    double distributor = 0.0;
    for (double volume : sales.channels[0].volume_l) distributor += volume;
    CHECK(distributor == doctest::Approx(150.0));
    CHECK(sales.channels[1].channel == Channel::Taproom);
    REQUIRE(sales.beers.size() == 1);
    CHECK(sales.beers[0].beer == "Red Ale");
    CHECK(sales.beers[0].units.at(PackageType::HalfBarrel) == 5);
    CHECK(sales.beers[0].revenue == doctest::Approx(450.0 + kTaproomRevenue));

    const app::BeerDetail detail = insights.beer_detail("red ale", kSeptember);
    REQUIRE(detail.batches.size() == 1);
    CHECK(detail.batches[0].volume_l == doctest::Approx(250.0));
    REQUIRE(detail.customers.size() == 2);
}

TEST_CASE("stock lasts as long as its selling rate, and brewing must start a tank time before") {
    Brewery brewery;
    app::InsightsService insights = brewery.insights(brewery.admin);
    insights.refresh();
    // On September 11: 250 L sold in the last 8 weeks, 31.25 L a week; 250 L left.
    const std::vector<app::BeerRunway> stock = insights.stock({2026, 9, 11});
    REQUIRE(stock.size() == 1);
    CHECK(stock[0].packaged_l == doctest::Approx(250.0));
    CHECK(stock[0].tank_days == 3);
    CHECK(stock[0].runway.days_left == 56);
    CHECK(stock[0].runway.brew_by == domain::Date{2026, 11, 3});
}

TEST_CASE("production shows what was brewed and packaged, and where the rest went") {
    Brewery brewery;
    app::InsightsService insights = brewery.insights(brewery.admin);
    insights.refresh();
    const app::ProductionInsights production = insights.production(kSeptember);
    CHECK(production.packaged_l.now == doctest::Approx(500.0));
    REQUIRE(production.batches.size() == 1);
    CHECK(production.batches[0].lost_share() == doctest::Approx(20.0 / 520.0));
    CHECK(production.batches[0].days_in_tanks == 3);

    const app::IngredientInsights ingredients = insights.ingredients(kSeptember);
    REQUIRE(ingredients.items.size() == 1);
    CHECK(ingredients.items[0].used == doctest::Approx(50.0));
    CHECK(ingredients.items[0].on_hand == doctest::Approx(50.0));
    REQUIRE(ingredients.suppliers.size() == 1);
    CHECK(ingredients.suppliers[0].second == doctest::Approx(200.0));
}

TEST_CASE("the profit and loss takes the cost of goods, excise, losses and expenses from revenue") {
    Brewery brewery;
    app::InsightsService insights = brewery.insights(brewery.admin);
    insights.refresh();
    const app::ProfitLoss statement = insights.profit_loss(kSeptember);
    const app::ProfitLossColumn& september = statement.now;
    CHECK(september.revenue.at(Channel::Distributor) == doctest::Approx(450.0));
    CHECK(september.revenue.at(Channel::Taproom) == doctest::Approx(kTaproomRevenue));
    CHECK(september.cost_of_goods == doctest::Approx(50.0));
    CHECK(september.excise == near(2.13 * 3.50, 0.01));  // 250 L is 2.13 barrels, at the small brewer's rate
    CHECK(september.total_expenses == doctest::Approx(1000.0));
    CHECK(september.net == doctest::Approx(september.total_revenue - 50.0 - september.excise - 1000.0));
    REQUIRE(statement.before);
    CHECK(statement.before->total_revenue == 0.0);
    REQUIRE(statement.beers.size() == 1);
    CHECK(*statement.beers[0].margin() == doctest::Approx((september.total_revenue - 50.0) / september.total_revenue));
    CHECK(statement.unpriced_lines == 0);

    const std::vector<app::BatchCostRow> costs = insights.batch_costs();
    REQUIRE(costs.size() == 1);
    CHECK(costs[0].revenue == doctest::Approx(450.0 + kTaproomRevenue));
    CHECK(costs[0].cost_sold == doctest::Approx(50.0));
}

TEST_CASE("brewers see volumes, not money") {
    Brewery brewery;
    app::UserService users(brewery.db, app::PasswordCost{1024, 1});
    users.set_password(brewery.admin, brewery.admin.user_id, "admin");
    const app::Session brewer = *users.sign_in(users.add(brewery.admin, "Sam", domain::Role::Brewer, ""), "");
    app::InsightsService insights = brewery.insights(brewer);
    insights.refresh();
    const app::Overview overview = insights.overview(kSeptember);
    CHECK(overview.sold_l.now == doctest::Approx(250.0));
    CHECK_FALSE(overview.revenue);
    CHECK_FALSE(overview.inventory_value);
    CHECK(insights.sales(kSeptember).beers[0].revenue == 0.0);
    CHECK_THROWS_AS(insights.profit_loss(kSeptember), app::NotAllowed);
    CHECK_THROWS_AS(insights.batch_costs(), app::NotAllowed);
}
