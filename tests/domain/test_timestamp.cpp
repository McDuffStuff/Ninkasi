#include "domain/timestamp.h"
#include "test_helpers.h"

using namespace ninkasi::domain;

TEST_CASE("timestamps round-trip through the stored ISO-8601 form") {
    const std::optional<Timestamp> time = parse_iso8601("2026-10-27T14:03:09Z");
    REQUIRE(time);
    CHECK(to_iso8601(*time) == "2026-10-27T14:03:09Z");

    // The Unix epoch, and a leap day.
    CHECK(parse_iso8601("1970-01-01T00:00:00Z")->time_since_epoch().count() == 0);
    CHECK(to_iso8601(*parse_iso8601("2028-02-29T23:59:59Z")) == "2028-02-29T23:59:59Z");

    // Seconds are optional.
    CHECK(to_iso8601(*parse_iso8601("2026-10-27T14:03Z")) == "2026-10-27T14:03:00Z");
}

TEST_CASE("malformed timestamps are rejected") {
    CHECK_FALSE(parse_iso8601(""));
    CHECK_FALSE(parse_iso8601("2026-10-27"));
    CHECK_FALSE(parse_iso8601("2026-10-27 14:03:00"));    // no T or Z
    CHECK_FALSE(parse_iso8601("2026-10-27T14:03:00+02"));  // only UTC is stored
    CHECK_FALSE(parse_iso8601("2026-13-01T00:00:00Z"));
    CHECK_FALSE(parse_iso8601("2026-02-30T00:00:00Z"));
    CHECK_FALSE(parse_iso8601("2026-10-27T24:00:00Z"));
    CHECK_FALSE(parse_iso8601("2026-10-27T1a:00:00Z"));
}

TEST_CASE("local times convert to and from timestamps") {
    // Midday avoids the hour that doesn't exist when clocks go forward.
    const LocalTime local{2026, 7, 14, 12, 30};
    CHECK(to_local(from_local(local)) == local);

    // Rolling over: minute 90 is half past the next hour.
    CHECK(to_local(from_local({2026, 7, 14, 12, 90})) == LocalTime{2026, 7, 14, 13, 30});
}

TEST_CASE("dates parse, compare and count days") {
    CHECK(parse_date("2026-10-30") == Date{2026, 10, 30});
    CHECK_FALSE(parse_date("2026-02-29"));  // not a leap year
    CHECK(parse_date("2028-02-29"));
    CHECK_FALSE(parse_date("2026-10-3"));
    CHECK_FALSE(parse_date("30/10/2026"));
    CHECK(to_iso8601(Date{2026, 1, 5}) == "2026-01-05");

    CHECK(Date{2026, 10, 30} < Date{2026, 11, 1});
    CHECK(add_days({2026, 12, 30}, 3) == Date{2027, 1, 2});
    CHECK(add_days({2026, 3, 1}, -1) == Date{2026, 2, 28});
    CHECK(days_between({2026, 10, 27}, {2026, 11, 3}) == 7);
    CHECK(days_between({2026, 11, 3}, {2026, 10, 27}) == -7);

    CHECK(local_date(from_local({2026, 7, 14, 23, 59})) == Date{2026, 7, 14});
}

TEST_CASE("calendar facts for the date picker") {
    CHECK(weekday({2026, 10, 27}) == 2);  // a Tuesday
    CHECK(weekday({2026, 11, 1}) == 0);   // a Sunday
    CHECK(days_in_month(2026, 2) == 28);
    CHECK(days_in_month(2028, 2) == 29);
    CHECK(days_in_month(2026, 12) == 31);
    CHECK(days_in_month(2026, 11) == 30);
}
