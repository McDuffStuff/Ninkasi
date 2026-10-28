#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/inventory_service.h"
#include "app/packaged_goods_service.h"
#include "app/session.h"
#include "app/ttb_service.h"
#include "calc/units.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "test_helpers.h"

#include <chrono>
#include <string>

using namespace ninkasi;
using domain::PackageType;
using domain::ReportColumn;

namespace {

constexpr double kBarrel = 31.0 * calc::kLitersPerUsGallon;  // liters

app::Session migrated_session(data::Database& db) {
    data::migrate(db);
    return app::open_local_session(db);
}

// Mid-October 2026 plus `days`: well inside the fourth quarter in any time zone.
domain::Timestamp october(int days) {
    return *domain::parse_iso8601("2026-10-15T12:00:00Z") + std::chrono::hours(24 * days);
}

struct Brewery {
    data::Database db{":memory:"};
    app::Session session = migrated_session(db);
    data::SettingsRepository settings{db};
    app::FloorService floor{db, session};
    app::InventoryService inventory{db, session};
    app::PackagedGoodsService goods{db, session};
    app::BatchService batches{db, session, floor, inventory, goods};
    app::TtbService ttb{db, session, settings};
    std::int64_t brewhouse = floor.add(domain::ElementType::Brewhouse, 0.0, 0.0);
    std::int64_t fermenter = floor.add(domain::ElementType::Fermenter, 0.0, 0.0);
};

} // namespace

TEST_CASE("a quarter's report and tax come from what was brewed, packaged and sold") {
    Brewery brewery;
    domain::Recipe recipe;
    recipe.name = "Red Ale";
    // 20 bbl brewed; 30 half-barrels (15 bbl) packaged, so 5 bbl lost; 20 sold.
    brewery.batches.log_brew({.brewhouse_id = brewery.brewhouse, .at = october(0), .code = "1", .recipe = recipe},
                             app::KnockOut{.vessel_id = brewery.fermenter, .at = october(0), .volume_l = 20 * kBarrel});
    brewery.batches.package({.vessel_id = brewery.fermenter, .at = october(5), .half_barrels = 30});
    brewery.goods.remove({.beer = "Red Ale", .type = PackageType::HalfBarrel,
                          .unit_volume_l = domain::keg_volume_l(PackageType::HalfBarrel), .count = 20,
                          .reason = domain::PackagedReason::Sold, .at = october(17)});

    const domain::ReportPeriod quarter = domain::period_containing({2026, 11, 1}, domain::ReportCadence::Quarterly);
    const app::TtbReport report = brewery.ttb.report(quarter);
    const domain::ReportOfOperations& lines = report.operations;
    CHECK(lines.at(1, ReportColumn::Total) == 0);
    CHECK(lines.at(2, ReportColumn::Cellar) == 2000);
    CHECK(lines.at(22, ReportColumn::Cellar) == 1500);
    CHECK(lines.at(9, ReportColumn::Kegs) == 1500);
    CHECK(lines.at(30, ReportColumn::Cellar) == 500);
    CHECK(lines.at(14, ReportColumn::Kegs) == 1000);
    CHECK(lines.at(33, ReportColumn::Cellar) == 0);
    CHECK(lines.at(33, ReportColumn::Kegs) == 500);
    CHECK(lines.at(13, ReportColumn::Total) == lines.at(34, ReportColumn::Total));
    CHECK(report.tax.barrels == near(10.0, 1e-9));
    CHECK(report.tax.tax == near(35.0, 1e-9));

    const std::string csv = brewery.ttb.csv(report);
    CHECK(csv.find("14,Removed for consumption or sale,0.00,0.00,10.00,0.00,0.00,10.00") != std::string::npos);
    CHECK(csv.find("\"Removed without payment of tax for use in research, development, or testing\"") !=
          std::string::npos);

    // December alone: November's sales count toward the year's first 60,000 barrels.
    brewery.goods.remove({.beer = "Red Ale", .type = PackageType::HalfBarrel,
                          .unit_volume_l = domain::keg_volume_l(PackageType::HalfBarrel), .count = 2,
                          .reason = domain::PackagedReason::Taproom, .at = october(50)});
    const app::TtbReport december =
        brewery.ttb.report(domain::period_containing({2026, 12, 15}, domain::ReportCadence::Monthly));
    CHECK(december.earlier_taxable_barrels == near(10.0, 1e-9));
    CHECK(december.operations.at(15, ReportColumn::Kegs) == 100);
    CHECK(december.operations.at(1, ReportColumn::Kegs) == 500);
}

TEST_CASE("TTB settings are remembered") {
    Brewery brewery;
    CHECK(brewery.ttb.cadence() == domain::ReportCadence::Quarterly);
    CHECK(brewery.ttb.small_brewer());
    brewery.ttb.set_cadence(domain::ReportCadence::Monthly);
    brewery.ttb.set_small_brewer(false);
    brewery.ttb.set_brewery_number("BR-CA-12345");

    app::TtbService again{brewery.db, brewery.session, brewery.settings};
    CHECK(again.cadence() == domain::ReportCadence::Monthly);
    CHECK_FALSE(again.small_brewer());
    CHECK(again.brewery_number() == "BR-CA-12345");
}
