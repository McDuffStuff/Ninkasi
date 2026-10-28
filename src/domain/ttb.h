#pragma once
// TTB reporting: the Brewer's Report of Operations (TTB F 5130.9, Part 1)
// and the federal excise tax on beer, built from the volume ledger (beer in
// tanks) and the packaged goods ledger (kegs and cans).
//
// The report follows TTB F 5130.9 (lines 1 to 36, as revised in 2013),
// checked against TTB's published form and instructions. Beer is reported in
// barrels of 31 US gallons, rounded to hundredths. Beer moves through three
// areas, each with its own columns: the cellar (bulk beer in tanks), racking
// (bulk beer being kegged, then the kegs) and bottling (bulk beer being
// canned or bottled, then the cases). Every barrel that enters a column is
// either removed on a later line or still on hand at the end.
//
// Each ledger row is rounded to hundredths of a barrel once, and everything
// is added up from those, so a report always balances (line 13 equals line
// 34) and its closing stock (line 33) is the next report's opening (line 1).

#include "domain/batch.h"
#include "domain/packaging.h"
#include "domain/timestamp.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ninkasi::domain {

// --- Reporting periods ------------------------------------------------------------

// How often the brewery reports. TTB F 5130.9 is filed monthly, or quarterly
// by brewers liable for no more than $50,000 a year in beer tax; the excise
// tax return can be filed annually by brewers liable for no more than $1,000.
enum class ReportCadence { Monthly, Quarterly, Annual };
inline constexpr std::array kReportCadences = {ReportCadence::Monthly, ReportCadence::Quarterly,
                                               ReportCadence::Annual};

struct ReportPeriod {
    Date first;  // the first day of the period
    Date last;   // the last day

    bool operator==(const ReportPeriod&) const = default;
};

// The period of `cadence` that `date` falls in.
ReportPeriod period_containing(Date date, ReportCadence cadence);
// The period before or after `period`.
ReportPeriod previous_period(const ReportPeriod& period, ReportCadence cadence);
ReportPeriod next_period(const ReportPeriod& period, ReportCadence cadence);
// "October 2026", "Q4 2026 (October–December)", "2026".
std::string period_label(const ReportPeriod& period, ReportCadence cadence);
// Local midnight at the start of `first` and after `last`: the period's
// moments are from <= t < to.
Timestamp period_start(const ReportPeriod& period);
Timestamp period_end(const ReportPeriod& period);

// --- The Brewer's Report of Operations ----------------------------------------------

// The form's columns (b) to (g).
enum class ReportColumn {
    Cellar,        // (b) cellar, bulk
    RackingBulk,   // (c) racking, bulk
    Kegs,          // (d) racking, kegs
    BottlingBulk,  // (e) bottling, bulk
    Cases,         // (f) bottling, cases (cans and bottles)
    Total,         // (g) the line's columns added up
};
inline constexpr std::size_t kReportColumns = 6;
inline constexpr int kReportLines = 34;  // lines 35 and 36 are adjustments to earlier reports

// Hundredths of a barrel: the report's precision, kept as whole numbers so
// sums are exact.
using Centibarrels = std::int64_t;
Centibarrels to_centibarrels(double liters);
double to_barrels(Centibarrels amount);

struct ReportOfOperations {
    // [line][column], line 0 unused.
    std::array<std::array<Centibarrels, kReportColumns>, kReportLines + 1> lines{};

    Centibarrels at(int line, ReportColumn column) const {
        return lines[static_cast<std::size_t>(line)][static_cast<std::size_t>(column)];
    }
    // Barrels taxed in the period: removed for consumption or sale (line 14)
    // and for the brewery's tavern (line 15).
    Centibarrels taxable() const;
};

// TTB's wording for a line of Part 1.
std::string_view report_line_text(int line);
// Lines 2 to 12 add beer, 14 to 32 remove it.
bool is_addition_line(int line);

// The report for moments from <= t < to, from every row of both ledgers
// (rows after `to` are ignored).
ReportOfOperations report_of_operations(std::span<const VolumeEvent> cellar, std::span<const PackagedMovement> packaged,
                                        Timestamp from, Timestamp to);

// --- Excise tax ---------------------------------------------------------------------
// 26 U.S.C. 5051(a), as amended in 2020 (permanent): $3.50 a barrel on the
// first 60,000 barrels a calendar year for a domestic brewer producing no
// more than 2,000,000 barrels a year; otherwise $16 a barrel on the first
// 6,000,000 barrels and $18 after that. Fractions of a barrel are taxed at
// the same rate.

inline constexpr double kReducedRatePerBarrel = 3.50;
inline constexpr double kReducedRateBarrels = 60'000.0;
inline constexpr double kMiddleRatePerBarrel = 16.00;
inline constexpr double kMiddleRateBarrels = 6'000'000.0;
inline constexpr double kFullRatePerBarrel = 18.00;

struct ExciseTax {
    double barrels = 0.0;  // taxed in the period
    double at_reduced = 0.0;
    double at_middle = 0.0;
    double at_full = 0.0;
    double tax = 0.0;      // dollars
};

// The tax on `barrels` removed in a period, when `earlier_barrels` were
// already removed earlier in the same calendar year. `small_brewer`: the
// brewery qualifies for the $3.50 rate.
ExciseTax excise_tax(double barrels, double earlier_barrels, bool small_brewer);

// --- Names and stored keys --------------------------------------------------------

std::string_view name(ReportCadence cadence);
std::string_view key(ReportCadence cadence);
std::optional<ReportCadence> report_cadence_from_key(std::string_view text);

} // namespace ninkasi::domain
