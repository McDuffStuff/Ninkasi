#include "app/batch_service.h"
#include "app/csv.h"
#include "app/export_service.h"
#include "app/floor_service.h"
#include "app/inventory_service.h"
#include "app/packaged_goods_service.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <chrono>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

using namespace ninkasi;
namespace fs = std::filesystem;

namespace {

constexpr double kBarrel = 31.0 * calc::kLitersPerUsGallon;  // liters

app::Session migrated_session(data::Database& db) {
    data::migrate(db);
    return app::open_local_session(db);
}

domain::Timestamp day(int n) {
    return *domain::parse_iso8601("2026-10-15T12:00:00Z") + std::chrono::hours(24 * n);
}

// A brewery that brewed 20 bbl of Red Ale, packaged 30 half-barrels and
// received an invoice of malt; units are a US brewery's, with gravity in SG.
struct Brewery {
    data::Database db{":memory:"};
    app::Session session = migrated_session(db);
    calc::UnitPreferences units = calc::UnitPreferences::us_brewery();
    app::FloorService floor{db, session};
    app::InventoryService inventory{db, session};
    app::PackagedGoodsService goods{db, session};
    app::BatchService batches{db, session, floor, inventory, goods};
    app::ExportService exports{db, units, session};

    Brewery() {
        units.set(calc::Measure::Gravity, calc::Unit::SpecificGravity);
        const std::int64_t brewhouse = floor.add(domain::ElementType::Brewhouse, 0.0, 0.0);
        const std::int64_t fermenter = floor.add(domain::ElementType::Fermenter, 0.0, 0.0);
        floor.rename(fermenter, "FV1");
        domain::Recipe recipe;
        recipe.name = "Red Ale";
        recipe.style = "Irish Red Ale";
        batches.log_brew({.brewhouse_id = brewhouse, .at = day(0), .code = "40", .recipe = recipe},
                         app::KnockOut{.vessel_id = fermenter, .at = day(0), .volume_l = 20 * kBarrel, .og = 1.052});
        batches.package({.vessel_id = fermenter, .at = day(10), .half_barrels = 30});

        domain::InventoryItem malt;
        malt.name = "Pale malt, 2-row";
        const std::int64_t malt_id = inventory.add_item(malt);
        inventory.receive({.received_at = day(1), .supplier = "Country Malt", .number = "INV-7",
                           .lines = {{.item_id = malt_id, .quantity = 500.0, .cost = 650.0}}});
    }
};

int lines(const std::string& text) {
    int count = 0;
    for (char c : text) count += c == '\n';
    return count;
}

} // namespace

TEST_CASE("CSV fields are quoted only when they need it") {
    CHECK(app::csv_field("Red Ale") == "Red Ale");
    CHECK(app::csv_field("Pale malt, 2-row") == "\"Pale malt, 2-row\"");
    CHECK(app::csv_field("a \"good\" beer") == "\"a \"\"good\"\" beer\"");
    CHECK(app::csv_row({"a", "b,c"}) == "a,\"b,c\"\r\n");
}

TEST_CASE("tables export with names, readable keys and the brewery's units") {
    Brewery brewery;

    const std::string batches = brewery.exports.csv("batches");
    CHECK(batches.starts_with("Batch,Beer,Style,Status,Brewed,Finished,Volume brewed (bbl),OG (SG),"));
    CHECK(batches.find("40,Red Ale,Irish Red Ale,Packaged,") != std::string::npos);
    CHECK(batches.find(",20.00,1.052,") != std::string::npos);

    // Brewed, kegged and the loss: three rows after the header.
    const std::string ledger = brewery.exports.csv("volume_ledger");
    CHECK(lines(ledger) == 4);
    CHECK(ledger.find(",40,Red Ale,FV1,-15.00,Kegged,Local Brewer") != std::string::npos);

    const std::string goods = brewery.exports.csv("packaged_goods");
    CHECK(goods.find("Red Ale,40,Half barrel,58.674,30,15.00") != std::string::npos);

    const std::string invoices = brewery.exports.csv("invoices");
    CHECK(invoices.find("Country Malt,INV-7,\"Pale malt, 2-row\",500.000,kg,650.00") != std::string::npos);

    // Activity text shows its quantities in barrels.
    CHECK(brewery.exports.csv("activity").find("20.00 bbl") != std::string::npos);

    CHECK_THROWS_AS(brewery.exports.csv("spaceships"), std::invalid_argument);
}

TEST_CASE("every table exports, and all of them can be saved at once") {
    Brewery brewery;
    for (const app::ExportTable& table : brewery.exports.tables()) {
        CAPTURE(table.key);
        CHECK_FALSE(brewery.exports.csv(table.key).empty());
    }

    const fs::path folder = fs::temp_directory_path() / ("ninkasi-export-" + std::to_string(std::random_device{}()));
    const std::vector<fs::path> files = brewery.exports.save_all(folder);
    CHECK(files.size() == brewery.exports.tables().size());
    for (const fs::path& file : files) CHECK(fs::exists(file));
    CHECK(fs::exists(folder / "Batches.csv"));
    std::error_code error;
    fs::remove_all(folder, error);
}

TEST_CASE("only admins export the brewery's money") {
    Brewery brewery;
    app::UserService users(brewery.db, app::PasswordCost{1024, 1});
    users.set_password(brewery.session, brewery.session.user_id, "admin");
    const app::Session brewer = *users.sign_in(users.add(brewery.session, "Sam", domain::Role::Brewer, ""), "");
    app::ExportService exports(brewery.db, brewery.units, brewer);
    bool sales = false;
    bool customers = false;
    for (const app::ExportTable& table : exports.tables()) {
        sales = sales || table.key == "sales";
        customers = customers || table.key == "customers";
    }
    CHECK_FALSE(sales);
    CHECK(customers);
    CHECK_THROWS_AS(exports.csv("expenses"), app::NotAllowed);
    CHECK(brewery.exports.csv("sales").find("Price each") != std::string::npos);
}
