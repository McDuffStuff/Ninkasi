#include "app/csv.h"
#include "app/pos_import.h"
#include "test_helpers.h"

#include <string>
#include <vector>

using namespace ninkasi;
using app::DateOrder;
using domain::Date;

TEST_CASE("point-of-sale dates are read whatever their order") {
    CHECK(app::parse_pos_date("2026-10-28", DateOrder::MonthDayYear) == Date{2026, 10, 28});
    CHECK(app::parse_pos_date("2026-10-28T17:45:00Z", DateOrder::DayMonthYear) == Date{2026, 10, 28});
    CHECK(app::parse_pos_date("10/28/2026 5:45 PM", DateOrder::MonthDayYear) == Date{2026, 10, 28});
    CHECK(app::parse_pos_date("28/10/2026", DateOrder::DayMonthYear) == Date{2026, 10, 28});
    CHECK(app::parse_pos_date("28.10.26", DateOrder::DayMonthYear) == Date{2026, 10, 28});
    CHECK(app::parse_pos_date(" 3/4/26 ", DateOrder::MonthDayYear) == Date{2026, 3, 4});
    CHECK_FALSE(app::parse_pos_date("28/10/2026", DateOrder::MonthDayYear));  // no 28th month
    CHECK_FALSE(app::parse_pos_date("Total", DateOrder::MonthDayYear));
    CHECK_FALSE(app::parse_pos_date("2026-02-30", DateOrder::YearMonthDay));
}

TEST_CASE("point-of-sale amounts are read however they're written") {
    CHECK(*app::parse_pos_amount("$1,234.50") == doctest::Approx(1234.5));
    CHECK(*app::parse_pos_amount("1.234,50 €") == doctest::Approx(1234.5));
    CHECK(*app::parse_pos_amount("1,234") == doctest::Approx(1234.0));
    CHECK(*app::parse_pos_amount("12,5") == doctest::Approx(12.5));
    CHECK(*app::parse_pos_amount("-$7.00") == doctest::Approx(-7.0));
    CHECK(*app::parse_pos_amount("($7.00)") == doctest::Approx(-7.0));
    CHECK(*app::parse_pos_amount("2") == doctest::Approx(2.0));
    CHECK_FALSE(app::parse_pos_amount(""));
    CHECK_FALSE(app::parse_pos_amount("n/a"));
}

TEST_CASE("an export's columns are guessed from its header and remembered") {
    const std::string text =
        "Date,Time,Category,Item,Qty,Gross Sales,Discounts,Net Sales\r\n"
        "10/27/2026,17:02,Beer,Red Ale Pint,2,$14.00,$0.00,$14.00\r\n"
        "10/27/2026,17:40,Beer,\"Hazy IPA, 4-pack\",1,$18.00,-$2.00,$16.00\r\n"
        "10/28/2026,18:15,Beer,Red Ale Pint,3,$21.00,$0.00,$21.00\r\n"
        ",,,Total,6,$53.00,-$2.00,$51.00\r\n";
    const std::vector<std::vector<std::string>> table = app::parse_csv(text);
    const app::PosColumns columns = app::guess_pos_columns(table);
    CHECK(columns.date == 0);
    CHECK(columns.item == 3);
    CHECK(columns.quantity == 4);
    CHECK(columns.amount == 7);  // net, not gross
    CHECK(columns.dates == DateOrder::MonthDayYear);
    CHECK(app::pos_columns_from_text(app::to_text(columns)) == columns);
    CHECK(app::header_signature(table.front()) == "date|time|category|item|qty|gross sales|discounts|net sales");

    const app::PosTable rows = app::read_pos_rows(table, columns);
    CHECK(rows.skipped == 1);  // the total
    REQUIRE(rows.rows.size() == 3);
    CHECK(rows.rows[1] == domain::PosRow{{2026, 10, 27}, "Hazy IPA, 4-pack", 1.0, 16.0});
    CHECK(rows.rows[2].day == Date{2026, 10, 28});

    // Days first: a first number over 12 means day/month/year.
    const std::vector<std::vector<std::string>> european = {{"Date", "Item", "Amount"},
                                                            {"02/10/2026", "Pils", "4,50"},
                                                            {"28/10/2026", "Pils", "4,50"}};
    const app::PosColumns guessed = app::guess_pos_columns(european);
    CHECK(guessed.dates == DateOrder::DayMonthYear);
}
