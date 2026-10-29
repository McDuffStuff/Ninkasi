#include "domain/analytics.h"
#include "test_helpers.h"

#include <vector>

using namespace ninkasi;
using domain::Date;
using domain::Grouping;
using domain::Period;

TEST_CASE("preset periods end today, and are compared with the one before or a year before") {
    const Date today{2026, 10, 29};
    CHECK(domain::preset_period(domain::PeriodPreset::ThisMonth, today) == Period{{2026, 10, 1}, today});
    CHECK(domain::preset_period(domain::PeriodPreset::LastThreeMonths, today) == Period{{2026, 8, 1}, today});
    CHECK(domain::preset_period(domain::PeriodPreset::YearToDate, today) == Period{{2026, 1, 1}, today});
    CHECK(domain::preset_period(domain::PeriodPreset::LastTwelveMonths, today) == Period{{2025, 11, 1}, today});

    const Period october{{2026, 10, 1}, {2026, 10, 31}};
    CHECK(october.days() == 31);
    CHECK(*domain::comparison_period(october, domain::Comparison::PreviousPeriod) ==
          Period{{2026, 8, 31}, {2026, 9, 30}});
    CHECK(*domain::comparison_period(october, domain::Comparison::YearBefore) == Period{{2025, 10, 1}, {2025, 10, 31}});
    CHECK_FALSE(domain::comparison_period(october, domain::Comparison::None));
    const Period leap{{2028, 2, 1}, {2028, 2, 29}};
    CHECK(domain::comparison_period(leap, domain::Comparison::YearBefore)->to == Date{2027, 2, 28});
}

TEST_CASE("a period is split into weeks from Monday, months or quarters") {
    // Thursday October 1 to Saturday October 31, 2026.
    const Period october{{2026, 10, 1}, {2026, 10, 31}};
    const std::vector<domain::Bucket> weeks = domain::buckets(october, Grouping::Week);
    REQUIRE(weeks.size() == 5);
    CHECK(weeks[0].first == Date{2026, 10, 1});
    CHECK(weeks[0].last == Date{2026, 10, 4});  // the Sunday
    CHECK(weeks[0].label == "Sep 28");          // its week's Monday
    CHECK(weeks[1].first == Date{2026, 10, 5});
    CHECK(weeks[4].last == Date{2026, 10, 31});
    CHECK(*domain::bucket_index(weeks, {2026, 10, 12}) == 2);
    CHECK_FALSE(domain::bucket_index(weeks, {2026, 11, 1}));

    const std::vector<domain::Bucket> months = domain::buckets({{2026, 8, 15}, {2026, 10, 29}}, Grouping::Month);
    REQUIRE(months.size() == 3);
    CHECK(months[0].first == Date{2026, 8, 15});
    CHECK(months[0].last == Date{2026, 8, 31});
    CHECK(months[2].label == "Oct 2026");

    const std::vector<domain::Bucket> quarters = domain::buckets({{2025, 11, 1}, {2026, 10, 29}}, Grouping::Quarter);
    REQUIRE(quarters.size() == 5);
    CHECK(quarters[0].label == "Q4 2025");
    CHECK(quarters[1].first == Date{2026, 1, 1});
    CHECK(quarters[4].last == Date{2026, 10, 29});

    CHECK(domain::default_grouping(october) == Grouping::Week);
    CHECK(domain::default_grouping({{2025, 11, 1}, {2026, 10, 29}}) == Grouping::Month);
}

TEST_CASE("rolling averages, changes and medians") {
    const std::vector<double> values{4.0, 8.0, 6.0, 2.0};
    const std::vector<double> averages = domain::rolling_average(values, 2);
    CHECK(averages == std::vector<double>{4.0, 6.0, 7.0, 4.0});
    CHECK(*domain::change(125.0, 100.0) == doctest::Approx(0.25));
    CHECK(*domain::change(-50.0, -100.0) == doctest::Approx(0.5));
    CHECK_FALSE(domain::change(10.0, 0.0));
    CHECK(domain::median({5.0, 1.0, 3.0}) == 3.0);
    CHECK(domain::median({4.0, 1.0, 3.0, 2.0}) == 2.5);
}

TEST_CASE("stock runs out at its selling rate, and more must be brewed a tank time before") {
    CHECK(domain::per_week(80.0, 56) == doctest::Approx(10.0));
    const std::vector<int> recent{30, 18, 21, 24, 20, 22};
    CHECK(domain::tank_days(recent, 21) == 21);  // the median of the last five
    CHECK(domain::tank_days({}, 21) == 21);

    const Date today{2026, 10, 29};
    // 60 L at 10 L a week: 42 days. A three-week batch must start in 21 days.
    domain::Runway runway = domain::runway(60.0, 10.0, today, 21);
    CHECK(runway.days_left == 42);
    CHECK(runway.runs_out == Date{2026, 12, 10});
    CHECK(runway.brew_by == Date{2026, 11, 19});
    CHECK(runway.status == domain::RunwayStatus::Plenty);
    CHECK(domain::runway(40.0, 10.0, today, 21).status == domain::RunwayStatus::BrewSoon);  // brew in 7 days
    CHECK(domain::runway(20.0, 10.0, today, 21).status == domain::RunwayStatus::BrewNow);
    CHECK(domain::runway(0.0, 10.0, today, 21).status == domain::RunwayStatus::Out);
    CHECK(domain::runway(50.0, 0.0, today, 21).status == domain::RunwayStatus::NotSelling);
}

TEST_CASE("a tank is in use from when it's filled until it's empty") {
    const domain::Timestamp start = domain::start_of({2026, 10, 5});  // a Monday
    const auto at = [&](int hours) { return start + std::chrono::hours(hours); };
    const std::vector<domain::VolumeEvent> events{
        {1, at(0), 1, 7, 1000.0, domain::VolumeReason::Brewed},
        {2, at(24 * 3), 1, 7, -990.0, domain::VolumeReason::TransferOut},
        {3, at(24 * 3), 1, 7, -10.0, domain::VolumeReason::Loss},
        {4, at(24 * 5), 2, 7, 500.0, domain::VolumeReason::Brewed},
    };
    const std::vector<domain::Occupancy> spans = domain::occupancy(events, at(24 * 7));
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].to == at(24 * 3));
    CHECK(spans[1].to == at(24 * 7));  // still full

    const std::vector<domain::Bucket> week = domain::buckets({{2026, 10, 5}, {2026, 10, 11}}, Grouping::Week);
    const std::vector<double> share = domain::occupied_share(spans, week);
    REQUIRE(share.size() == 1);
    CHECK(share[0] == doctest::Approx(5.0 / 7.0));
}
