#include "app/costing_service.h"
#include "app/inventory_service.h"
#include "app/packaged_goods_service.h"
#include "app/sales_service.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/inventory_repository.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "test_helpers.h"

#include <chrono>

using namespace ninkasi;
using domain::PackageType;

namespace {

domain::Timestamp day(int n) {
    return *domain::parse_iso8601("2026-09-01T09:00:00Z") + std::chrono::hours(24 * n);
}

// A brewery that bought 100 kg of malt for $200, brewed a batch of Red Ale
// with 50 kg of it, and packaged 10 kegs of 50 L.
struct Brewery {
    data::Database db{":memory:"};
    app::Session admin;
    data::SettingsRepository settings{db};
    std::optional<app::InventoryService> inventory;
    std::optional<app::PackagedGoodsService> goods;
    std::int64_t batch = 0;

    Brewery() {
        data::migrate(db);
        admin = app::open_local_session(db);
        inventory.emplace(db, admin);
        goods.emplace(db, admin);

        domain::InventoryItem malt;
        malt.name = "Pale malt";
        const std::int64_t item = inventory->add_item(malt);
        domain::Invoice invoice;
        invoice.received_at = day(1);
        invoice.supplier = "Maltings";
        invoice.lines.push_back({item, 100.0, 200.0, "", std::nullopt});
        inventory->receive(invoice);

        db.exec("INSERT INTO batches (code, name, recipe, status) VALUES ('7', 'Red Ale', '{}', 'packaged');");
        batch = db.last_insert_id();
        data::InventoryRepository stock(db);
        const std::int64_t lot = stock.lots(item).front().lot.id;
        data::Transaction tx(db);
        stock.insert_movement({.at = day(2), .item_id = item, .lot_id = lot, .quantity = -50.0,
                               .reason = domain::StockReason::Brewed, .batch_id = batch},
                              std::nullopt, admin.user_id);
        goods->write_packaged(batch, std::nullopt, PackageType::HalfBarrel, 50.0, 10, day(3), "");
        tx.commit();
        goods->refresh();
    }
};

} // namespace

TEST_CASE("what was sold is costed from the invoices its ingredients came on") {
    Brewery brewery;
    app::SalesService sales(brewery.db, brewery.admin, brewery.settings, *brewery.goods);
    domain::Sale sale;
    sale.at = day(4);
    domain::SaleLine kegs;
    kegs.beer = "Red Ale";
    kegs.package = PackageType::HalfBarrel;
    kegs.unit_volume_l = 50.0;
    kegs.quantity = 3;
    sale.lines.push_back(kegs);
    sales.record_sale(sale, false);
    brewery.goods->remove({.beer = "Red Ale", .type = PackageType::HalfBarrel, .unit_volume_l = 50.0, .count = 1,
                           .reason = domain::PackagedReason::Destroyed, .at = day(5)});

    app::CostingService costing(brewery.db, brewery.admin);
    costing.refresh(1);
    // $100 of malt over 500 L packaged: $10 a keg.
    const domain::BatchCost& cost = costing.batches().at(brewery.batch);
    CHECK(cost.ingredients == doctest::Approx(100.0));
    CHECK(*cost.package_cost(PackageType::HalfBarrel, 50.0) == doctest::Approx(10.0));

    const app::CostingService::PeriodCosts september = costing.period_costs(day(0), day(10));
    CHECK(september.sold == doctest::Approx(30.0));
    CHECK(september.lost == doctest::Approx(10.0));
    CHECK(september.uncosted == 0);
    CHECK(costing.period_costs(day(5), day(10)).sold == 0.0);

    const std::int64_t line = sales.sales().front().lines.front().id;
    CHECK(costing.sale_line_costs().at(line) == doctest::Approx(30.0));

    // 50 kg of malt left at $2, and 6 kegs at $10.
    const domain::InventoryValue value = costing.inventory_value();
    CHECK(value.stock == doctest::Approx(100.0));
    CHECK(value.packaged_beer == doctest::Approx(60.0));
    CHECK(value.total() == doctest::Approx(160.0));
}

TEST_CASE("only admins see what the beer cost") {
    Brewery brewery;
    app::UserService users(brewery.db, app::PasswordCost{1024, 1});
    users.set_password(brewery.admin, brewery.admin.user_id, "admin");
    const app::Session brewer =
        *users.sign_in(users.add(brewery.admin, "Sam", domain::Role::Brewer, ""), "");
    app::CostingService costing(brewery.db, brewer);
    CHECK_FALSE(costing.allowed());
    CHECK_THROWS_AS(costing.reload(), app::NotAllowed);
    CHECK_THROWS_AS(costing.inventory_value(), app::NotAllowed);
}
