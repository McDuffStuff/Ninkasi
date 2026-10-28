#pragma once
// TTB reporting: the Brewer's Report of Operations and the excise tax due for
// a period, worked out from the ledgers when asked for (see domain/ttb.h),
// and the brewery's TTB settings: how often it reports, whether it pays the
// small brewer rate, and its TTB brewery number.

#include "data/database.h"
#include "data/settings_repository.h"
#include "data/ttb_repository.h"
#include "domain/ttb.h"

#include <string>
#include <string_view>

namespace ninkasi::app {

struct TtbReport {
    domain::ReportPeriod period;
    domain::ReportCadence cadence = domain::ReportCadence::Quarterly;
    domain::ReportOfOperations operations;
    double earlier_taxable_barrels = 0.0;  // taxed earlier in the same calendar year
    domain::ExciseTax tax;
};

class TtbService {
public:
    TtbService(data::Database& db, data::SettingsRepository& settings);

    // --- Settings, saved as they change ------------------------------------------
    domain::ReportCadence cadence() const { return cadence_; }
    void set_cadence(domain::ReportCadence cadence);
    // Whether the brewery qualifies for the $3.50 rate: a domestic brewer
    // producing no more than 2,000,000 barrels a year.
    bool small_brewer() const { return small_brewer_; }
    void set_small_brewer(bool small_brewer);
    // The TTB brewery number, "BR-CA-12345", printed on exports.
    const std::string& brewery_number() const { return brewery_number_; }
    void set_brewery_number(std::string_view number);

    // --- Reports --------------------------------------------------------------------
    TtbReport report(const domain::ReportPeriod& period);
    // The report as CSV: Part 1 line by line in barrels, then the excise tax.
    std::string csv(const TtbReport& report) const;

private:
    data::SettingsRepository& settings_;
    data::TtbRepository repository_;
    domain::ReportCadence cadence_ = domain::ReportCadence::Quarterly;
    bool small_brewer_ = true;
    std::string brewery_number_;
};

} // namespace ninkasi::app
