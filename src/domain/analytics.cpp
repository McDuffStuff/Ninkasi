#include "domain/analytics.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ninkasi::domain {

namespace {

constexpr const char* kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

const char* short_month(int month) {
    return kShortMonths[std::clamp(month, 1, 12) - 1];
}

// The same day `years` years on; February 29 becomes the 28th.
Date years_on(Date date, int years) {
    Date moved{date.year + years, date.month, date.day};
    moved.day = std::min(moved.day, days_in_month(moved.year, moved.month));
    return moved;
}

Date month_start(Date date) {
    return {date.year, date.month, 1};
}

Date months_on(Date first_of_month, int months) {
    const int index = first_of_month.year * 12 + first_of_month.month - 1 + months;
    return {index / 12, index % 12 + 1, 1};
}

} // namespace

// --- Periods ------------------------------------------------------------------------

Period preset_period(PeriodPreset preset, Date today) {
    switch (preset) {
    case PeriodPreset::ThisMonth:        return {month_start(today), today};
    case PeriodPreset::LastThreeMonths:  return {months_on(month_start(today), -2), today};
    case PeriodPreset::YearToDate:       return {{today.year, 1, 1}, today};
    case PeriodPreset::LastTwelveMonths: return {months_on(month_start(today), -11), today};
    }
    return {month_start(today), today};
}

std::optional<Period> comparison_period(const Period& period, Comparison comparison) {
    switch (comparison) {
    case Comparison::None: return std::nullopt;
    case Comparison::PreviousPeriod: {
        const Date to = add_days(period.from, -1);
        return Period{add_days(to, -(period.days() - 1)), to};
    }
    case Comparison::YearBefore: return Period{years_on(period.from, -1), years_on(period.to, -1)};
    }
    return std::nullopt;
}

Timestamp start_of(Date date) {
    return from_local({date.year, date.month, date.day, 0, 0});
}

Timestamp end_of(Date date) {
    return start_of(add_days(date, 1));
}

// --- Buckets ------------------------------------------------------------------------

std::vector<Bucket> buckets(const Period& period, Grouping grouping) {
    std::vector<Bucket> result;
    Date first = period.from;
    while (first <= period.to) {
        Bucket bucket;
        bucket.first = first;
        Date next{};
        switch (grouping) {
        case Grouping::Week: {
            // The Monday after: weekday() is 0 for Sunday.
            const int to_monday = (8 - weekday(first)) % 7;
            next = add_days(first, to_monday == 0 ? 7 : to_monday);
            const Date monday = add_days(next, -7);
            bucket.label = std::string(short_month(monday.month)) + " " + std::to_string(monday.day);
            break;
        }
        case Grouping::Month:
            next = months_on(month_start(first), 1);
            bucket.label = std::string(short_month(first.month)) + " " + std::to_string(first.year);
            break;
        case Grouping::Quarter: {
            const int quarter = (first.month - 1) / 3;
            next = months_on({first.year, quarter * 3 + 1, 1}, 3);
            bucket.label = "Q" + std::to_string(quarter + 1) + " " + std::to_string(first.year);
            break;
        }
        }
        bucket.last = std::min(add_days(next, -1), period.to);
        result.push_back(std::move(bucket));
        first = next;
    }
    return result;
}

std::optional<std::size_t> bucket_index(std::span<const Bucket> buckets, Date date) {
    // Buckets are in order: the first whose last day is on or after `date`.
    const auto found = std::lower_bound(buckets.begin(), buckets.end(), date,
                                        [](const Bucket& bucket, Date day) { return bucket.last < day; });
    if (found == buckets.end() || date < found->first) return std::nullopt;
    return static_cast<std::size_t>(found - buckets.begin());
}

Grouping default_grouping(const Period& period) {
    if (period.days() <= 100) return Grouping::Week;
    if (period.days() <= 731) return Grouping::Month;
    return Grouping::Quarter;
}

// --- Series -------------------------------------------------------------------------

std::vector<double> rolling_average(std::span<const double> values, int window) {
    std::vector<double> averages;
    const std::size_t width = static_cast<std::size_t>(std::max(window, 1));
    double sum = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        sum += values[i];
        if (i >= width) sum -= values[i - width];
        averages.push_back(sum / static_cast<double>(std::min(i + 1, width)));
    }
    return averages;
}

std::optional<double> change(double now, double before) {
    if (std::abs(before) < 1e-12) return std::nullopt;
    return (now - before) / std::abs(before);
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

// --- Rates and runway -------------------------------------------------------------------

double per_week(double amount, int days) {
    return days > 0 ? amount * 7.0 / days : 0.0;
}

int tank_days(std::span<const int> recent_days, int fallback) {
    if (recent_days.empty()) return fallback;
    const std::size_t count = std::min<std::size_t>(recent_days.size(), 5);
    std::vector<double> latest;
    for (std::size_t i = recent_days.size() - count; i < recent_days.size(); ++i) {
        latest.push_back(recent_days[i]);
    }
    return static_cast<int>(std::lround(median(latest)));
}

Runway runway(double on_hand, double per_week_sold, Date today, int tank_days_needed, int soon_days) {
    Runway result;
    result.on_hand = on_hand;
    result.per_week = per_week_sold;
    if (!(on_hand > 0.0)) {
        result.status = per_week_sold > 0.0 ? RunwayStatus::Out : RunwayStatus::NotSelling;
        if (per_week_sold > 0.0) result.days_left = 0;
        return result;
    }
    if (!(per_week_sold > 0.0)) {
        result.status = RunwayStatus::NotSelling;
        return result;
    }
    const int days = static_cast<int>(std::floor(on_hand / per_week_sold * 7.0));
    result.days_left = days;
    result.runs_out = add_days(today, days);
    result.brew_by = add_days(*result.runs_out, -tank_days_needed);
    const int until_brew = days_between(today, *result.brew_by);
    if (until_brew < 0) {
        result.status = RunwayStatus::BrewNow;
    } else if (until_brew <= soon_days) {
        result.status = RunwayStatus::BrewSoon;
    } else {
        result.status = RunwayStatus::Plenty;
    }
    return result;
}

// --- Tank use ---------------------------------------------------------------------------

std::vector<Occupancy> occupancy(std::span<const VolumeEvent> events, Timestamp now) {
    std::vector<const VolumeEvent*> ordered;
    for (const VolumeEvent& event : events) ordered.push_back(&event);
    std::stable_sort(ordered.begin(), ordered.end(), [](const VolumeEvent* a, const VolumeEvent* b) {
        return a->at != b->at ? a->at < b->at : a->id < b->id;
    });
    std::vector<Occupancy> spans;
    double volume = 0.0;
    std::optional<Timestamp> since;
    for (const VolumeEvent* event : ordered) {
        volume += event->volume_l;
        const bool full = volume > kEmptyVolumeL;
        if (full && !since) since = event->at;
        if (!full && since) {
            spans.push_back({*since, event->at});
            since.reset();
        }
    }
    if (since) spans.push_back({*since, std::max(*since, now)});
    return spans;
}

std::vector<double> occupied_share(std::span<const Occupancy> spans, std::span<const Bucket> buckets) {
    std::vector<double> shares;
    for (const Bucket& bucket : buckets) {
        const Timestamp from = start_of(bucket.first);
        const Timestamp to = end_of(bucket.last);
        double held = 0.0;
        for (const Occupancy& span : spans) {
            const Timestamp a = std::max(span.from, from);
            const Timestamp b = std::min(span.to, to);
            if (b > a) held += static_cast<double>((b - a).count());
        }
        const double length = static_cast<double>((to - from).count());
        shares.push_back(length > 0.0 ? std::min(held / length, 1.0) : 0.0);
    }
    return shares;
}

// --- Names ----------------------------------------------------------------------------

std::string_view name(PeriodPreset preset) {
    switch (preset) {
    case PeriodPreset::ThisMonth:        return "This month";
    case PeriodPreset::LastThreeMonths:  return "Last 3 months";
    case PeriodPreset::YearToDate:       return "Year to date";
    case PeriodPreset::LastTwelveMonths: return "Last 12 months";
    }
    return "This month";
}

std::string_view name(Comparison comparison) {
    switch (comparison) {
    case Comparison::None:           return "No comparison";
    case Comparison::PreviousPeriod: return "Previous period";
    case Comparison::YearBefore:     return "Same time last year";
    }
    return "No comparison";
}

std::string_view name(Grouping grouping) {
    switch (grouping) {
    case Grouping::Week:    return "By week";
    case Grouping::Month:   return "By month";
    case Grouping::Quarter: return "By quarter";
    }
    return "By month";
}

std::string_view name(RunwayStatus status) {
    switch (status) {
    case RunwayStatus::NotSelling: return "Not selling";
    case RunwayStatus::Plenty:     return "Plenty";
    case RunwayStatus::BrewSoon:   return "Brew soon";
    case RunwayStatus::BrewNow:    return "Brew now";
    case RunwayStatus::Out:        return "Out";
    }
    return "Plenty";
}

} // namespace ninkasi::domain
