#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/inventory_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <chrono>
#include <stdexcept>
#include <string>

using namespace ninkasi;
using domain::StockReason;

namespace {

app::Session migrated_session(data::Database& db) {
    data::migrate(db);
    return app::open_local_session(db);
}

domain::Timestamp day(int n) {
    return *domain::parse_iso8601("2026-10-01T08:00:00Z") + std::chrono::hours(24 * n);
}

struct Stores {
    data::Database db{":memory:"};
    app::Session session = migrated_session(db);
    app::FloorService floor{db, session};
    app::InventoryService inventory{db, session};

    std::int64_t item(const std::string& name, domain::StockUnit unit = domain::StockUnit::Mass) {
        domain::InventoryItem item;
        item.name = name;
        item.unit = unit;
        return inventory.add_item(item);
    }

    // A one-line invoice.
    std::int64_t receive(std::int64_t item_id, double quantity, int on_day) {
        return inventory.receive({.received_at = day(on_day), .lines = {{.item_id = item_id, .quantity = quantity}}});
    }

    double on_hand(std::int64_t item_id) const { return inventory.find(item_id)->on_hand; }

    double number(const std::string& sql) {
        auto stmt = db.prepare(sql);
        stmt.step();
        return stmt.column_double(0);
    }
};

} // namespace

TEST_CASE("an invoice adds a lot for each line") {
    Stores stores;
    const std::int64_t malt = stores.item("Pale malt");
    const std::int64_t hops = stores.item("Citra");
    CHECK(stores.on_hand(malt) == 0.0);

    const std::int64_t invoice = stores.inventory.receive(
        {.received_at = day(1),
         .supplier = " Country Malt ",
         .number = "INV-881",
         .lines = {{.item_id = malt, .quantity = 500.0, .cost = 650.0, .lot_code = "L123"},
                   {.item_id = hops, .quantity = 5.0, .cost = 120.0},
                   {.item_id = malt, .quantity = 250.0}}});
    CHECK(stores.on_hand(malt) == 750.0);
    CHECK(stores.on_hand(hops) == 5.0);
    const std::vector<data::LotRow> lots = stores.inventory.lots(malt);
    REQUIRE(lots.size() == 2);
    CHECK(lots[0].remaining == 500.0);
    CHECK(lots[0].lot.supplier == "Country Malt");
    CHECK(lots[0].lot.lot_code == "L123");
    CHECK(lots[0].lot.cost == 650.0);
    CHECK(lots[0].lot.invoice_id == invoice);

    const std::vector<data::InvoiceRow> invoices = stores.inventory.invoices();
    REQUIRE(invoices.size() == 1);
    CHECK(invoices[0].number == "INV-881");
    CHECK(invoices[0].lines == 3);
    CHECK(invoices[0].total == 770.0);
    const std::vector<data::InvoiceLineRow> lines = stores.inventory.invoice_lines(invoice);
    REQUIRE(lines.size() == 3);
    CHECK(lines[1].item_name == "Citra");

    // The log says which delivery stock came from.
    const std::vector<data::MovementRow> history = stores.inventory.movements(hops);
    REQUIRE(history.size() == 1);
    CHECK(history[0].supplier == "Country Malt");
    CHECK(history[0].invoice_number == "INV-881");
}

TEST_CASE("an invoice with a mistake saves nothing") {
    Stores stores;
    const std::int64_t malt = stores.item("Pale malt");
    const std::int64_t hops = stores.item("Citra");
    CHECK_THROWS_AS(stores.inventory.receive({.received_at = day(1)}), std::invalid_argument);
    CHECK_THROWS_AS(stores.inventory.receive({.received_at = day(1),
                                              .lines = {{.item_id = malt, .quantity = 500.0},
                                                        {.item_id = hops, .quantity = 0.0}}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(stores.receive(malt, -1.0, 1), std::invalid_argument);
    CHECK(stores.on_hand(malt) == 0.0);
    CHECK(stores.inventory.invoices().empty());
}

TEST_CASE("cans are counted and say what they hold") {
    Stores stores;
    domain::InventoryItem cans;
    cans.name = "16 oz cans";
    cans.category = domain::ItemCategory::Can;
    cans.unit = domain::StockUnit::Count;
    CHECK_THROWS_AS(stores.inventory.add_item(cans), std::invalid_argument);
    cans.fill_volume_l = 0.473;
    const std::int64_t id = stores.inventory.add_item(cans);
    CHECK(stores.inventory.find(id)->item.fill_volume_l == 0.473);

    cans.id = id;
    cans.unit = domain::StockUnit::Mass;
    CHECK_THROWS_AS(stores.inventory.update_item(cans), std::invalid_argument);

    // Only cans keep a fill volume.
    domain::InventoryItem labels;
    labels.name = "Labels";
    labels.category = domain::ItemCategory::Packaging;
    labels.unit = domain::StockUnit::Count;
    labels.fill_volume_l = 1.0;
    CHECK_FALSE(stores.inventory.find(stores.inventory.add_item(labels))->item.fill_volume_l);
}

TEST_CASE("stock is used first in, first out") {
    Stores stores;
    const std::int64_t malt = stores.item("Pale malt");
    stores.receive(malt, 100.0, 1);
    stores.receive(malt, 200.0, 2);
    stores.inventory.take(malt, 150.0, StockReason::Used, day(3), "test mash");

    CHECK(stores.on_hand(malt) == 150.0);
    const std::vector<data::LotRow> lots = stores.inventory.lots(malt);
    CHECK(lots[0].remaining == 0.0);
    CHECK(lots[1].remaining == 150.0);
    CHECK_THROWS_AS(stores.inventory.take(malt, 5.0, StockReason::Received, day(3), ""), std::invalid_argument);
}

TEST_CASE("stock used before its delivery was logged is settled by the delivery") {
    Stores stores;
    const std::int64_t hops = stores.item("Citra");
    stores.inventory.take(hops, 30.0, StockReason::Used, day(1), "");
    CHECK(stores.on_hand(hops) == -30.0);

    stores.receive(hops, 100.0, 2);
    CHECK(stores.on_hand(hops) == near(70.0, 1e-9));
    CHECK(stores.inventory.lots(hops)[0].remaining == near(70.0, 1e-9));
    CHECK(stores.number("SELECT SUM(quantity) FROM stock_movements WHERE lot_id IS NULL") == near(0.0, 1e-9));
}

TEST_CASE("a stock count replaces the recorded stock") {
    Stores stores;
    const std::int64_t salt = stores.item("Gypsum");
    stores.receive(salt, 10.0, 1);
    stores.inventory.count(salt, 9.0, day(2), "");
    CHECK(stores.on_hand(salt) == near(9.0, 1e-9));
    CHECK(stores.inventory.lots(salt)[0].remaining == near(9.0, 1e-9));

    stores.inventory.count(salt, 12.0, day(3), "found a bag");
    CHECK(stores.on_hand(salt) == near(12.0, 1e-9));
    const std::vector<data::MovementRow> history = stores.inventory.movements(salt);
    REQUIRE(history.size() == 3);
    CHECK(history.front().movement.reason == StockReason::Counted);
    CHECK(history.front().movement.note == "found a bag");
}

TEST_CASE("items need distinct names, and items with history are archived") {
    Stores stores;
    const std::int64_t malt = stores.item("Pale malt");
    CHECK_THROWS_AS(stores.item("pale MALT"), std::invalid_argument);
    CHECK_THROWS_AS(stores.item("  "), std::invalid_argument);

    const std::int64_t unused = stores.item("Rice hulls");
    stores.inventory.remove_item(unused);
    CHECK(stores.number("SELECT COUNT(*) FROM inventory_items WHERE name = 'Rice hulls'") == 0.0);

    stores.receive(malt, 50.0, 1);
    stores.inventory.remove_item(malt);
    CHECK_FALSE(stores.inventory.find(malt));
    CHECK(stores.number("SELECT archived FROM inventory_items WHERE name = 'Pale malt'") == 1.0);
    CHECK_NOTHROW(stores.item("Pale malt"));  // the name is free again
}

TEST_CASE("a knock-out draws the confirmed ingredients from stock") {
    Stores stores;
    app::BatchService batches{stores.db, stores.session, stores.floor, stores.inventory};
    const std::int64_t brewhouse = stores.floor.add(domain::ElementType::Brewhouse, 0.0, 0.0);
    const std::int64_t fermenter = stores.floor.add(domain::ElementType::Fermenter, 0.0, 0.0);
    const std::int64_t malt = stores.item("Pale malt");
    const std::int64_t hops = stores.item("cascade");
    stores.receive(malt, 250.0, 1);
    stores.receive(hops, 0.5, 1);

    domain::Recipe recipe;
    recipe.name = "Pale Ale";
    recipe.fermentables.push_back({0, {.name = "Pale malt"}, 200.0});
    recipe.hops.push_back({.hop = {.name = "Cascade"}, .mass_kg = 1.0});
    recipe.miscs.push_back({"Whirlfloc", "1 tablet", domain::MiscUse::Boil, 15.0});
    std::vector<domain::IngredientUse> used = domain::planned_ingredients(recipe);
    used[0].actual = 210.0;  // a little more malt than planned

    batches.log_brew({.brewhouse_id = brewhouse, .at = day(2), .code = "1", .recipe = recipe},
                     app::KnockOut{.vessel_id = fermenter, .at = day(2), .volume_l = 1000.0, .ingredients = used});

    CHECK(stores.on_hand(malt) == near(40.0, 1e-9));
    CHECK(stores.on_hand(hops) == near(-0.5, 1e-9));  // more was used than was on record
    // Each draw records the batch and the brew ingredient it went into.
    CHECK(stores.number("SELECT COUNT(*) FROM stock_movements WHERE reason = 'brewed' AND batch_id IS NOT NULL "
                        "AND brew_ingredient_id IS NOT NULL") == 3.0);
}

TEST_CASE("a knock-out that fails takes nothing from stock") {
    Stores stores;
    app::BatchService batches{stores.db, stores.session, stores.floor, stores.inventory};
    const std::int64_t brewhouse = stores.floor.add(domain::ElementType::Brewhouse, 0.0, 0.0);
    const std::int64_t fermenter = stores.floor.add(domain::ElementType::Fermenter, 0.0, 0.0);
    const std::int64_t malt = stores.item("Pale malt");
    stores.receive(malt, 250.0, 1);

    domain::Recipe recipe;
    recipe.name = "Pale Ale";
    recipe.fermentables.push_back({0, {.name = "Pale malt"}, 200.0});
    const std::int64_t brew = batches.start_brew({.brewhouse_id = brewhouse, .at = day(2), .code = "1", .recipe = recipe});
    stores.db.exec("CREATE TRIGGER fail_ledger BEFORE INSERT ON volume_events BEGIN SELECT RAISE(ABORT, 'disk full'); END;");

    CHECK_THROWS(batches.knock_out({.brew_id = brew, .vessel_id = fermenter, .at = day(2), .volume_l = 1000.0,
                                    .ingredients = domain::planned_ingredients(recipe)}));
    stores.inventory.refresh();
    CHECK(stores.on_hand(malt) == 250.0);
}
