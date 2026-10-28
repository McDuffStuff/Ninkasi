#include "app/ttb_service.h"

#include <cstdio>

namespace ninkasi::app {

namespace {

constexpr const char* kCadenceKey = "ttb.cadence";
constexpr const char* kSmallBrewerKey = "ttb.small_brewer";
constexpr const char* kBreweryNumberKey = "ttb.brewery_number";

// A CSV field, quoted when it holds a comma or a quote.
std::string field(std::string_view text) {
    if (text.find_first_of(",\"\n") == std::string_view::npos) return std::string(text);
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + "\"";
}

std::string number(double value, int decimals) {
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

} // namespace

TtbService::TtbService(data::Database& db, data::SettingsRepository& settings)
    : settings_(settings), repository_(db) {
    cadence_ = domain::report_cadence_from_key(settings_.get_or(kCadenceKey, "quarterly"))
                   .value_or(domain::ReportCadence::Quarterly);
    small_brewer_ = settings_.get_or(kSmallBrewerKey, "1") != "0";
    brewery_number_ = settings_.get_or(kBreweryNumberKey, "");
}

void TtbService::set_cadence(domain::ReportCadence cadence) {
    cadence_ = cadence;
    settings_.set(kCadenceKey, domain::key(cadence));
}

void TtbService::set_small_brewer(bool small_brewer) {
    small_brewer_ = small_brewer;
    settings_.set(kSmallBrewerKey, small_brewer ? "1" : "0");
}

void TtbService::set_brewery_number(std::string_view number) {
    brewery_number_ = std::string(number);
    settings_.set(kBreweryNumberKey, brewery_number_);
}

TtbReport TtbService::report(const domain::ReportPeriod& period) {
    const domain::Timestamp start = domain::period_start(period);
    const domain::Timestamp end = domain::period_end(period);
    const std::vector<domain::VolumeEvent> cellar = repository_.volume_events(end);
    const std::vector<domain::PackagedMovement> packaged = repository_.packaged_movements(end);

    TtbReport report;
    report.period = period;
    report.cadence = cadence_;
    report.operations = domain::report_of_operations(cellar, packaged, start, end);

    // The small brewer rate covers the first 60,000 barrels of the calendar
    // year, so the tax depends on what was removed earlier in the year.
    const domain::Timestamp year_start = domain::period_start({{period.first.year, 1, 1}, period.first});
    if (year_start < start) {
        report.earlier_taxable_barrels =
            domain::to_barrels(domain::report_of_operations(cellar, packaged, year_start, start).taxable());
    }
    report.tax = domain::excise_tax(domain::to_barrels(report.operations.taxable()), report.earlier_taxable_barrels,
                                    small_brewer_);
    return report;
}

std::string TtbService::csv(const TtbReport& report) const {
    using domain::ReportColumn;
    std::string text;
    const auto row = [&text](std::initializer_list<std::string> fields) {
        bool first = true;
        for (const std::string& value : fields) {
            if (!first) text += ',';
            text += value;
            first = false;
        }
        text += "\r\n";
    };

    row({field("TTB F 5130.9 Brewer's Report of Operations"), field(domain::period_label(report.period, report.cadence))});
    row({"Brewery number", field(brewery_number_)});
    row({"Period", domain::to_iso8601(report.period.first), domain::to_iso8601(report.period.last)});
    row({});
    row({"Part 1 - Beer Summary (barrels)"});
    row({"Line", "Operations", field("(b) Cellar, bulk"), field("(c) Racking, bulk"), field("(d) Racking, keg"),
         field("(e) Bottling, bulk"), field("(f) Bottling, case"), field("(g) Totals")});
    for (int line = 1; line <= domain::kReportLines; ++line) {
        const auto cell = [&](ReportColumn column) {
            return number(domain::to_barrels(report.operations.at(line, column)), 2);
        };
        row({std::to_string(line), field(domain::report_line_text(line)), cell(ReportColumn::Cellar),
             cell(ReportColumn::RackingBulk), cell(ReportColumn::Kegs), cell(ReportColumn::BottlingBulk),
             cell(ReportColumn::Cases), cell(ReportColumn::Total)});
    }
    row({});
    row({"Excise tax"});
    row({field("Taxable removals, lines 14 and 15 (barrels)"), number(report.tax.barrels, 2)});
    row({field("Removed earlier this calendar year (barrels)"), number(report.earlier_taxable_barrels, 2)});
    row({field("Barrels at $3.50"), number(report.tax.at_reduced, 2)});
    row({field("Barrels at $16.00"), number(report.tax.at_middle, 2)});
    row({field("Barrels at $18.00"), number(report.tax.at_full, 2)});
    row({field("Tax ($)"), number(report.tax.tax, 2)});
    return text;
}

} // namespace ninkasi::app
