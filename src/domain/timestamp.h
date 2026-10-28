#pragma once
// Points in time and calendar dates.
//
// Moments (a brew, a reading) are Timestamps: UTC seconds, stored in the
// database as ISO-8601 text ("2026-10-27T14:03:00Z") and shown in the
// computer's local time zone. Due dates are plain local calendar Dates.

#include <chrono>
#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace ninkasi::domain {

using Timestamp = std::chrono::sys_seconds;

Timestamp now();

// "2026-10-27T14:03:00Z", the form stored in the database.
std::string to_iso8601(Timestamp time);
// Reads that form (seconds optional); nullopt if the text is malformed.
std::optional<Timestamp> parse_iso8601(std::string_view text);

// A wall-clock time in the local time zone.
struct LocalTime {
    int year = 1970;
    int month = 1;   // 1–12
    int day = 1;     // 1–31
    int hour = 0;    // 0–23
    int minute = 0;  // 0–59

    bool operator==(const LocalTime&) const = default;
};

LocalTime to_local(Timestamp time);
// Out-of-range fields roll over (minute 60 is the next hour), as in mktime.
Timestamp from_local(const LocalTime& local);

// A calendar date, without a time or time zone.
struct Date {
    int year = 1970;
    int month = 1;  // 1–12
    int day = 1;    // 1–31

    auto operator<=>(const Date&) const = default;
};

// Today in the local time zone.
Date today();
// The local date of a moment.
Date local_date(Timestamp time);
Date add_days(Date date, int days);
// Whole days from `from` to `to`: positive when `to` is later.
int days_between(Date from, Date to);
// 0 for Sunday through 6 for Saturday.
int weekday(Date date);
int days_in_month(int year, int month);

// "2026-10-30", the form stored in the database.
std::string to_iso8601(Date date);
// nullopt if the text is malformed or the date doesn't exist.
std::optional<Date> parse_date(std::string_view text);

} // namespace ninkasi::domain
