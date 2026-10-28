#include "calc/units.h"
#include "domain/ttb.h"
#include "test_helpers.h"

#include <chrono>
#include <vector>

using namespace ninkasi::domain;

namespace {

constexpr double kBarrel = 31.0 * ninkasi::calc::kLitersPerUsGallon;  // liters

Timestamp day(int n) {
    return *parse_iso8601("2026-10-01T08:00:00Z") + std::chrono::hours(24 * n);
}

Centibarrels cell(const ReportOfOperations& report, int line, ReportColumn column) {
    return report.at(line, column);
}

PackagedMovement packages(int on_day, PackagedReason reason, PackageType type, double unit_volume_l, int quantity) {
    PackagedMovement movement;
    movement.at = day(on_day);
    movement.reason = reason;
    movement.type = type;
    movement.unit_volume_l = unit_volume_l;
    movement.quantity = quantity;
    return movement;
}

} // namespace

TEST_CASE("reporting periods are calendar months, quarters and years") {
    const Date date{2026, 11, 14};
    CHECK(period_containing(date, ReportCadence::Monthly) == ReportPeriod{{2026, 11, 1}, {2026, 11, 30}});
    CHECK(period_containing(date, ReportCadence::Quarterly) == ReportPeriod{{2026, 10, 1}, {2026, 12, 31}});
    CHECK(period_containing(date, ReportCadence::Annual) == ReportPeriod{{2026, 1, 1}, {2026, 12, 31}});

    const ReportPeriod q1 = period_containing({2027, 2, 1}, ReportCadence::Quarterly);
    CHECK(previous_period(q1, ReportCadence::Quarterly) == ReportPeriod{{2026, 10, 1}, {2026, 12, 31}});
    CHECK(next_period(q1, ReportCadence::Quarterly).first == Date{2027, 4, 1});
    CHECK(period_label(q1, ReportCadence::Quarterly) == "Q1 2027 (January–March)");
    CHECK(period_label(period_containing(date, ReportCadence::Monthly), ReportCadence::Monthly) == "November 2026");
    // Periods follow each other without a gap.
    CHECK(period_end(q1) == period_start(next_period(q1, ReportCadence::Quarterly)));
    CHECK(report_cadence_from_key(key(ReportCadence::Annual)) == ReportCadence::Annual);
}

TEST_CASE("the report of operations follows beer from the cellar to removal") {
    const double half = keg_volume_l(PackageType::HalfBarrel);
    const std::vector<VolumeEvent> cellar = {
        {0, day(1), 1, 10, 20 * kBarrel, VolumeReason::Brewed},  // before the period
        {0, day(11), 2, 11, 10 * kBarrel, VolumeReason::Brewed},
        {0, day(12), 2, 11, -10 * kBarrel, VolumeReason::TransferOut},
        {0, day(12), 2, 12, 10 * kBarrel, VolumeReason::TransferIn},
        {0, day(15), 1, 10, -5 * kBarrel, VolumeReason::Kegged},  // counted from the kegs instead
        {0, day(15), 1, 10, -0.25 * kBarrel, VolumeReason::Loss},
        {0, day(25), 3, 10, 7 * kBarrel, VolumeReason::Brewed},  // after the period
    };
    const std::vector<PackagedMovement> packaged = {
        packages(5, PackagedReason::Packaged, PackageType::HalfBarrel, half, 4),  // before the period
        packages(15, PackagedReason::Packaged, PackageType::HalfBarrel, half, 10),
        packages(16, PackagedReason::Sold, PackageType::HalfBarrel, half, -4),
        packages(16, PackagedReason::Taproom, PackageType::HalfBarrel, half, -1),
        packages(17, PackagedReason::Packaged, PackageType::Can, 0.355, 24),
    };
    const ReportOfOperations report = report_of_operations(cellar, packaged, day(10), day(20));
    using enum ReportColumn;

    // Opening stock: 20 bbl brewed, 2 of it kegged.
    CHECK(cell(report, 1, Cellar) == 1800);
    CHECK(cell(report, 1, Kegs) == 200);
    CHECK(cell(report, 2, Cellar) == 1000);
    // 5 bbl racked: out of the cellar, through racking, into kegs.
    CHECK(cell(report, 22, Cellar) == 500);
    CHECK(cell(report, 6, RackingBulk) == 500);
    CHECK(cell(report, 25, RackingBulk) == 500);
    CHECK(cell(report, 9, Kegs) == 500);
    // 24 cans of 355 mL: 0.07 bbl.
    CHECK(cell(report, 23, Cellar) == 7);
    CHECK(cell(report, 10, Cases) == 7);
    CHECK(cell(report, 30, Cellar) == 25);
    CHECK(cell(report, 14, Kegs) == 200);
    CHECK(cell(report, 15, Kegs) == 50);
    CHECK(report.taxable() == 250);

    // Closing stock, and the report balances.
    CHECK(cell(report, 33, Cellar) == 1800 + 1000 - 500 - 7 - 25);
    CHECK(cell(report, 33, RackingBulk) == 0);
    CHECK(cell(report, 33, Kegs) == 200 + 500 - 200 - 50);
    CHECK(cell(report, 33, Cases) == 7);
    CHECK(cell(report, 13, Total) == cell(report, 34, Total));
    CHECK(cell(report, 13, Total) == 1800 + 1000 + 500 + 700 + 7 + 7);

    // The next period opens with this one's closing stock.
    const ReportOfOperations next = report_of_operations(cellar, packaged, day(20), day(30));
    for (ReportColumn column : {Cellar, RackingBulk, Kegs, BottlingBulk, Cases, Total}) {
        CHECK(cell(next, 1, column) == cell(report, 33, column));
    }
    CHECK(cell(next, 2, Cellar) == 700);
}

TEST_CASE("excise tax uses the small brewer rate for the year's first 60,000 barrels") {
    CHECK(excise_tax(100.0, 0.0, true).tax == near(350.0, 1e-9));
    const ExciseTax crossing = excise_tax(100.0, 59'950.0, true);
    CHECK(crossing.at_reduced == near(50.0, 1e-9));
    CHECK(crossing.at_middle == near(50.0, 1e-9));
    CHECK(crossing.tax == near(50 * 3.50 + 50 * 16.0, 1e-9));
    CHECK(excise_tax(100.0, 0.0, false).tax == near(1600.0, 1e-9));
    const ExciseTax big = excise_tax(10.0, 5'999'995.0, false);
    CHECK(big.at_middle == near(5.0, 1e-9));
    CHECK(big.at_full == near(5.0, 1e-9));
    CHECK(excise_tax(0.0, 0.0, true).tax == 0.0);
}
