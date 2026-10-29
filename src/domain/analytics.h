#pragma once
// The arithmetic behind the Insights window: the period looked at and the
// one it's compared with, the buckets a chart splits it into, rolling
// averages, how long stock lasts and when to brew more, and how much of the
// time each tank was in use.

#include "domain/batch.h"
#include "domain/timestamp.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

// --- Periods ------------------------------------------------------------------------

// Days from `from` to `to`, both included.
struct Period {
    Date from{};
    Date to{};

    int days() const { return days_between(from, to) + 1; }
    bool contains(Date date) const { return from <= date && date <= to; }
    bool operator==(const Period&) const = default;
};

enum class PeriodPreset { ThisMonth, LastThreeMonths, YearToDate, LastTwelveMonths };
inline constexpr std::array kPeriodPresets = {PeriodPreset::ThisMonth, PeriodPreset::LastThreeMonths,
                                              PeriodPreset::YearToDate, PeriodPreset::LastTwelveMonths};
// The preset period ending today (the last three and twelve months include
// this one).
Period preset_period(PeriodPreset preset, Date today);

// What a period is compared with.
enum class Comparison { None, PreviousPeriod, YearBefore };
inline constexpr std::array kComparisons = {Comparison::None, Comparison::PreviousPeriod, Comparison::YearBefore};
// The period just before, as long; or the same days a year before.
std::optional<Period> comparison_period(const Period& period, Comparison comparison);

// Local midnight at the start of `date`, and after it.
Timestamp start_of(Date date);
Timestamp end_of(Date date);

// --- Buckets ------------------------------------------------------------------------

enum class Grouping { Week, Month, Quarter };
inline constexpr std::array kGroupings = {Grouping::Week, Grouping::Month, Grouping::Quarter};

// Part of a period, a bar of a chart.
struct Bucket {
    Date first{};
    Date last{};
    std::string label;  // "Oct 5", "Oct 2026", "Q4 2026"
};

// The buckets covering a period: weeks start on Monday, and the first and
// last bucket are cut short by the period's ends.
std::vector<Bucket> buckets(const Period& period, Grouping grouping);
// The bucket `date` falls in; nullopt outside them all.
std::optional<std::size_t> bucket_index(std::span<const Bucket> buckets, Date date);
// Weeks for up to about three months, months for up to two years, quarters beyond.
Grouping default_grouping(const Period& period);

// --- Series -------------------------------------------------------------------------

// Each value's trailing mean over `window` values (fewer at the start).
std::vector<double> rolling_average(std::span<const double> values, int window);
// The change from `before` to `now` as a fraction (0.25 for 25 % up);
// nullopt when there's nothing before to compare with.
std::optional<double> change(double now, double before);
double median(std::vector<double> values);

// --- Rates and runway -------------------------------------------------------------------

// An amount over `days`, per week.
double per_week(double amount, int days);
// Days in tanks from brewing to packaging: the median of the most recent
// (up to five), else `fallback`.
int tank_days(std::span<const int> recent_days, int fallback);

enum class RunwayStatus {
    NotSelling,  // nothing sold lately
    Plenty,      // no need to brew yet
    BrewSoon,    // brew within the next two weeks
    BrewNow,     // more won't be ready before it runs out
    Out,         // none left
};

struct Runway {
    double on_hand = 0.0;
    double per_week = 0.0;  // sold, lately
    std::optional<int> days_left;
    std::optional<Date> runs_out;
    std::optional<Date> brew_by;  // so more is ready the day it runs out
    RunwayStatus status = RunwayStatus::NotSelling;
};

// How long `on_hand` lasts selling `per_week` from `today`, and when to brew
// so a batch `tank_days` in the making is ready by then. Brew-by within
// `soon_days` is BrewSoon.
Runway runway(double on_hand, double per_week, Date today, int tank_days, int soon_days = 14);

// --- Tank use ---------------------------------------------------------------------------

// A stretch of time a vessel held beer.
struct Occupancy {
    Timestamp from{};
    Timestamp to{};
};

// When a vessel held beer, from its volume events (any order). Still full
// at the end: until `now`.
std::vector<Occupancy> occupancy(std::span<const VolumeEvent> events, Timestamp now);
// The share of each bucket's time the vessel held beer (0 to 1).
std::vector<double> occupied_share(std::span<const Occupancy> spans, std::span<const Bucket> buckets);

// --- Names ----------------------------------------------------------------------------

std::string_view name(PeriodPreset preset);    // "This month"...
std::string_view name(Comparison comparison);  // "No comparison", "Previous period", "Same time last year"
std::string_view name(Grouping grouping);      // "By week"...
std::string_view name(RunwayStatus status);    // "Brew now"...

} // namespace ninkasi::domain
