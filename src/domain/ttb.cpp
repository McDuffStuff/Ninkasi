#include "domain/ttb.h"

#include "calc/units.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ninkasi::domain {

namespace {

constexpr double kLitersPerBarrel = 31.0 * calc::kLitersPerUsGallon;

constexpr const char* kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};

// TTB's wording for each line of Part 1; lines 12 and 32 are left blank on
// the form for the brewer to write in.
constexpr std::string_view kLineTexts[kReportLines + 1] = {
    "",
    "On hand beginning of this report period",
    "We produced by fermentation",
    "We added water and other liquids in cellar operations",
    "Beer received from racking and bottling",
    "Beer received in bond from other breweries and pilot brewing plants of same ownership",
    "Beer received from cellars",
    "Beer returned to this brewery after removal from this brewery",
    "Beer returned to this brewery after removal from another brewery of same ownership",
    "Racked",
    "Bottled",
    "Physical inventory disclosed an overage",
    "",
    "Total beer",
    "Removed for consumption or sale",
    "Removed tax-determined for use at tavern on brewery premises",
    "Removed without payment of tax for export",
    "Removed without payment of tax for use as supplies (vessels/aircraft)",
    "Removed without payment of tax for use in research, development, or testing",
    "Removed without payment of tax to other breweries and pilot brewing plants of same ownership",
    "Beer unfit for sale removed for use in manufacturing",
    "Beer consumed on premises (example: tasting room)",
    "Beer transferred for racking",
    "Beer transferred for bottling",
    "Beer returned to cellars",
    "Beer racked",
    "Beer bottled",
    "Laboratory samples",
    "Beer destroyed at brewery",
    "Beer transferred to a distilled spirits plant",
    "Recorded losses, including theft",
    "Physical inventory disclosed a shortage",
    "",
    "On hand end of period",
    "Total beer",
};

// The line packaged beer leaving (or coming back) for `reason` goes on.
int line_for(PackagedReason reason) {
    switch (reason) {
    case PackagedReason::Packaged:           return 0;  // racked or bottled: several lines, see below
    case PackagedReason::Returned:           return 7;
    case PackagedReason::Overage:            return 11;
    case PackagedReason::Sold:               return 14;
    case PackagedReason::Taproom:            return 15;
    case PackagedReason::Exported:           return 16;
    case PackagedReason::Supplies:           return 17;
    case PackagedReason::Research:           return 18;
    case PackagedReason::SameOwnership:      return 19;
    case PackagedReason::Unfit:              return 20;
    case PackagedReason::ConsumedOnPremises: return 21;
    case PackagedReason::LabSamples:         return 27;
    case PackagedReason::Destroyed:          return 28;
    case PackagedReason::Lost:               return 30;
    case PackagedReason::Shortage:           return 31;
    }
    return 0;
}

} // namespace

// --- Reporting periods ------------------------------------------------------------

ReportPeriod period_containing(Date date, ReportCadence cadence) {
    switch (cadence) {
    case ReportCadence::Monthly:
        return {{date.year, date.month, 1}, {date.year, date.month, days_in_month(date.year, date.month)}};
    case ReportCadence::Quarterly: {
        const int first_month = (date.month - 1) / 3 * 3 + 1;
        const int last_month = first_month + 2;
        return {{date.year, first_month, 1}, {date.year, last_month, days_in_month(date.year, last_month)}};
    }
    case ReportCadence::Annual:
        return {{date.year, 1, 1}, {date.year, 12, 31}};
    }
    return {date, date};
}

ReportPeriod previous_period(const ReportPeriod& period, ReportCadence cadence) {
    return period_containing(add_days(period.first, -1), cadence);
}

ReportPeriod next_period(const ReportPeriod& period, ReportCadence cadence) {
    return period_containing(add_days(period.last, 1), cadence);
}

std::string period_label(const ReportPeriod& period, ReportCadence cadence) {
    const std::string year = std::to_string(period.first.year);
    switch (cadence) {
    case ReportCadence::Monthly:
        return std::string(kMonthNames[period.first.month - 1]) + " " + year;
    case ReportCadence::Quarterly:
        return "Q" + std::to_string((period.first.month - 1) / 3 + 1) + " " + year + " (" +
               kMonthNames[period.first.month - 1] + "–" + kMonthNames[period.last.month - 1] + ")";
    case ReportCadence::Annual:
        return year;
    }
    return year;
}

Timestamp period_start(const ReportPeriod& period) {
    return from_local({period.first.year, period.first.month, period.first.day, 0, 0});
}

Timestamp period_end(const ReportPeriod& period) {
    const Date after = add_days(period.last, 1);
    return from_local({after.year, after.month, after.day, 0, 0});
}

// --- The Brewer's Report of Operations ----------------------------------------------

Centibarrels to_centibarrels(double liters) {
    return std::llround(liters / kLitersPerBarrel * 100.0);
}

double to_barrels(Centibarrels amount) {
    return static_cast<double>(amount) / 100.0;
}

Centibarrels ReportOfOperations::taxable() const {
    return at(14, ReportColumn::Total) + at(15, ReportColumn::Total);
}

std::string_view report_line_text(int line) {
    return line >= 1 && line <= kReportLines ? kLineTexts[line] : std::string_view();
}

bool is_addition_line(int line) {
    return line >= 2 && line <= 12;
}

ReportOfOperations report_of_operations(std::span<const VolumeEvent> cellar, std::span<const PackagedMovement> packaged,
                                        Timestamp from, Timestamp to) {
    ReportOfOperations report;
    std::array<Centibarrels, kReportColumns> opening{};

    // Puts a ledger row's barrels on a line: before the period it only
    // changes the opening stock.
    const auto post = [&](Timestamp at, int line, ReportColumn column, Centibarrels amount) {
        if (at >= to || amount == 0) return;
        const auto col = static_cast<std::size_t>(column);
        if (at < from) {
            opening[col] += is_addition_line(line) ? amount : -amount;
        } else {
            report.lines[static_cast<std::size_t>(line)][col] += amount;
        }
    };

    // Beer in tanks. Transfers between tanks stay in the cellar, and beer
    // leaving for kegs and cans is counted from the packages below.
    for (const VolumeEvent& event : cellar) {
        const Centibarrels amount = to_centibarrels(std::abs(event.volume_l));
        switch (event.reason) {
        case VolumeReason::Brewed:  post(event.at, 2, ReportColumn::Cellar, amount); break;
        case VolumeReason::Loss:    post(event.at, event.volume_l < 0.0 ? 30 : 11, ReportColumn::Cellar, amount); break;
        case VolumeReason::Dumped:  post(event.at, 28, ReportColumn::Cellar, amount); break;
        case VolumeReason::Adjusted:
            post(event.at, event.volume_l > 0.0 ? 11 : 31, ReportColumn::Cellar, amount);
            break;
        case VolumeReason::TransferIn:
        case VolumeReason::TransferOut:
        case VolumeReason::Kegged:
        case VolumeReason::Bottled:
            break;
        }
    }

    // Kegs and cans.
    for (const PackagedMovement& movement : packaged) {
        const Centibarrels amount = to_centibarrels(std::abs(movement.quantity * movement.unit_volume_l));
        const bool keg = is_keg(movement.type);
        if (movement.reason == PackagedReason::Packaged) {
            // Bulk beer goes from the cellar to racking (or bottling) and into packages.
            const ReportColumn bulk = keg ? ReportColumn::RackingBulk : ReportColumn::BottlingBulk;
            post(movement.at, keg ? 22 : 23, ReportColumn::Cellar, amount);
            post(movement.at, 6, bulk, amount);
            post(movement.at, keg ? 25 : 26, bulk, amount);
            post(movement.at, keg ? 9 : 10, keg ? ReportColumn::Kegs : ReportColumn::Cases, amount);
        } else {
            post(movement.at, line_for(movement.reason), keg ? ReportColumn::Kegs : ReportColumn::Cases, amount);
        }
    }

    // Opening stock, totals and closing stock, column by column.
    constexpr std::size_t kCellar = static_cast<std::size_t>(ReportColumn::Cellar);
    constexpr std::size_t kTotal = static_cast<std::size_t>(ReportColumn::Total);
    for (std::size_t col = kCellar; col < kTotal; ++col) {
        report.lines[1][col] = opening[col];
        Centibarrels in = 0;
        for (int line = 1; line <= 12; ++line) in += report.lines[static_cast<std::size_t>(line)][col];
        Centibarrels out = 0;
        for (int line = 14; line <= 32; ++line) out += report.lines[static_cast<std::size_t>(line)][col];
        report.lines[13][col] = in;
        report.lines[33][col] = in - out;
        report.lines[34][col] = out + report.lines[33][col];
    }
    for (auto& line : report.lines) {
        line[kTotal] = 0;
        for (std::size_t col = kCellar; col < kTotal; ++col) line[kTotal] += line[col];
    }
    return report;
}

// --- Excise tax ---------------------------------------------------------------------

ExciseTax excise_tax(double barrels, double earlier_barrels, bool small_brewer) {
    ExciseTax tax;
    tax.barrels = barrels;
    if (!(barrels > 0.0)) return tax;
    double left = barrels;
    double removed = std::max(earlier_barrels, 0.0);  // so far this calendar year
    // Fills a rate's band, which covers the year's barrels up to `limit`.
    const auto band = [&](double limit, double& at_rate) {
        const double taken = std::min(left, std::max(limit - removed, 0.0));
        at_rate += taken;
        left -= taken;
        removed += taken;
    };
    if (small_brewer) band(kReducedRateBarrels, tax.at_reduced);
    band(kMiddleRateBarrels, tax.at_middle);
    tax.at_full = left;
    tax.tax = tax.at_reduced * kReducedRatePerBarrel + tax.at_middle * kMiddleRatePerBarrel +
              tax.at_full * kFullRatePerBarrel;
    return tax;
}

// --- Names and stored keys --------------------------------------------------------

std::string_view name(ReportCadence cadence) {
    switch (cadence) {
    case ReportCadence::Monthly:   return "Monthly";
    case ReportCadence::Quarterly: return "Quarterly";
    case ReportCadence::Annual:    return "Annual";
    }
    return "Quarterly";
}

std::string_view key(ReportCadence cadence) {
    switch (cadence) {
    case ReportCadence::Monthly:   return "monthly";
    case ReportCadence::Quarterly: return "quarterly";
    case ReportCadence::Annual:    return "annual";
    }
    return "quarterly";
}

std::optional<ReportCadence> report_cadence_from_key(std::string_view text) {
    for (ReportCadence cadence : kReportCadences) {
        if (key(cadence) == text) return cadence;
    }
    return std::nullopt;
}

} // namespace ninkasi::domain
