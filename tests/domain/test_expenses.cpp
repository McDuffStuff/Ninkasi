#include "domain/expenses.h"
#include "test_helpers.h"

#include <vector>

using namespace ninkasi;
using domain::Date;
using domain::Month;

TEST_CASE("months count across years") {
    CHECK(domain::add_months({2026, 11}, 3) == Month{2027, 2});
    CHECK(domain::add_months({2026, 1}, -1) == Month{2025, 12});
    CHECK(domain::add_months({2026, 3}, -27) == Month{2023, 12});
    CHECK(domain::first_day({2026, 2}) == Date{2026, 2, 1});
    CHECK(domain::last_day({2028, 2}) == Date{2028, 2, 29});
    CHECK(domain::to_text({2026, 7}) == "2026-07");
    CHECK(domain::parse_month("2026-07") == Month{2026, 7});
    CHECK_FALSE(domain::parse_month("2026-13"));
    CHECK_FALSE(domain::parse_month("July"));
    CHECK(domain::month_name({2026, 10}) == "October 2026");
}

TEST_CASE("a period's expenses include each month of those that recur") {
    const std::vector<domain::Expense> dated{
        {1, {2026, 9, 30}, "Plumber", 7, 250.0, "", ""},
        {2, {2026, 10, 12}, "Print shop", 8, 80.0, "", ""},
    };
    const std::vector<domain::RecurringExpense> recurring{
        {1, 1, "Rent", 3000.0, {2026, 1}, std::nullopt},
        {2, 2, "Brewer's wage", 4000.0, {2026, 8}, Month{2026, 10}},
        {3, 2, "New hire", 3500.0, {2027, 1}, std::nullopt},
    };
    // Mid-September to the end of the year: October to December.
    const std::vector<domain::ExpenseLine> lines =
        domain::expenses_between(dated, recurring, {2026, 9, 15}, {2026, 12, 31});
    double rent = 0.0;
    double wages = 0.0;
    double other = 0.0;
    for (const domain::ExpenseLine& line : lines) {
        if (line.description == "Rent") rent += line.amount;
        else if (line.description == "Brewer's wage") wages += line.amount;
        else other += line.amount;
    }
    CHECK(rent == doctest::Approx(9000.0));   // October, November, December
    CHECK(wages == doctest::Approx(4000.0));  // October, its last
    CHECK(other == doctest::Approx(330.0));   // both dated ones
    REQUIRE(lines.size() == 6);
    CHECK(lines.front().day == Date{2026, 9, 30});
    CHECK(lines.back().day == Date{2026, 12, 1});
    CHECK(lines.back().recurring);

    // A period inside a month, without its first day: no rent.
    CHECK(domain::expenses_between({}, recurring, {2026, 11, 2}, {2026, 11, 30}).empty());
}
