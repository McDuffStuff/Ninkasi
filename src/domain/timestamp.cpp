#include "domain/timestamp.h"

#include <cstdio>
#include <ctime>

namespace ninkasi::domain {

namespace chr = std::chrono;

namespace {

// Reads `count` decimal digits starting at `pos`; -1 if any isn't a digit.
int read_digits(std::string_view text, std::size_t pos, std::size_t count) {
    if (pos + count > text.size()) return -1;
    int value = 0;
    for (std::size_t i = pos; i < pos + count; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

chr::sys_days to_sys_days(Date date) {
    return chr::sys_days(chr::year(date.year) / chr::month(static_cast<unsigned>(date.month)) /
                         chr::day(static_cast<unsigned>(date.day)));
}

Date from_sys_days(chr::sys_days days) {
    const chr::year_month_day ymd(days);
    return {static_cast<int>(ymd.year()), static_cast<int>(static_cast<unsigned>(ymd.month())),
            static_cast<int>(static_cast<unsigned>(ymd.day()))};
}

std::tm local_tm(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

} // namespace

Timestamp now() {
    return chr::floor<chr::seconds>(chr::system_clock::now());
}

std::string to_iso8601(Timestamp time) {
    const chr::sys_days day = chr::floor<chr::days>(time);
    const Date date = from_sys_days(day);
    const chr::hh_mm_ss clock(time - day);
    char text[32];
    std::snprintf(text, sizeof text, "%04d-%02d-%02dT%02d:%02d:%02dZ", date.year, date.month, date.day,
                  static_cast<int>(clock.hours().count()), static_cast<int>(clock.minutes().count()),
                  static_cast<int>(clock.seconds().count()));
    return text;
}

std::optional<Timestamp> parse_iso8601(std::string_view text) {
    // "YYYY-MM-DDTHH:MM:SSZ" or "YYYY-MM-DDTHH:MMZ".
    if (text.size() < 12 || text[10] != 'T' || text.back() != 'Z') return std::nullopt;
    const std::optional<Date> date = parse_date(text.substr(0, 10));
    if (!date) return std::nullopt;

    const std::string_view clock = text.substr(11, text.size() - 12);
    int hour = -1, minute = -1, second = 0;
    if (clock.size() == 8 && clock[2] == ':' && clock[5] == ':') {
        hour = read_digits(clock, 0, 2);
        minute = read_digits(clock, 3, 2);
        second = read_digits(clock, 6, 2);
    } else if (clock.size() == 5 && clock[2] == ':') {
        hour = read_digits(clock, 0, 2);
        minute = read_digits(clock, 3, 2);
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return std::nullopt;
    return to_sys_days(*date) + chr::hours(hour) + chr::minutes(minute) + chr::seconds(second);
}

LocalTime to_local(Timestamp time) {
    const std::tm tm = local_tm(chr::system_clock::to_time_t(time));
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min};
}

Timestamp from_local(const LocalTime& local) {
    std::tm tm{};
    tm.tm_year = local.year - 1900;
    tm.tm_mon = local.month - 1;
    tm.tm_mday = local.day;
    tm.tm_hour = local.hour;
    tm.tm_min = local.minute;
    tm.tm_isdst = -1;  // let the C library work out daylight saving time
    return chr::floor<chr::seconds>(chr::system_clock::from_time_t(std::mktime(&tm)));
}

Date today() {
    return local_date(now());
}

Date local_date(Timestamp time) {
    const LocalTime local = to_local(time);
    return {local.year, local.month, local.day};
}

Date add_days(Date date, int count) {
    return from_sys_days(to_sys_days(date) + chr::days(count));
}

int days_between(Date from, Date to) {
    return static_cast<int>((to_sys_days(to) - to_sys_days(from)).count());
}

int weekday(Date date) {
    return static_cast<int>(chr::weekday(to_sys_days(date)).c_encoding());
}

int days_in_month(int year, int month) {
    const chr::year_month_day_last last(chr::year(year), chr::month_day_last(chr::month(static_cast<unsigned>(month))));
    return static_cast<int>(static_cast<unsigned>(last.day()));
}

std::string to_iso8601(Date date) {
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", date.year, date.month, date.day);
    return text;
}

std::optional<Date> parse_date(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
    const int year = read_digits(text, 0, 4);
    const int month = read_digits(text, 5, 2);
    const int day = read_digits(text, 8, 2);
    if (year < 0 || month < 0 || day < 0) return std::nullopt;
    const chr::year_month_day ymd(chr::year(year), chr::month(static_cast<unsigned>(month)),
                                  chr::day(static_cast<unsigned>(day)));
    if (!ymd.ok()) return std::nullopt;
    return Date{year, month, day};
}

} // namespace ninkasi::domain
