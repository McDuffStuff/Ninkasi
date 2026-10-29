#include "domain/money.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace ninkasi;
using domain::Currency;
using domain::format_money;

TEST_CASE("money is written with its symbol and grouped thousands") {
    const Currency dollars;
    CHECK(format_money(1234.5, dollars) == "$1,234.50");
    CHECK(format_money(0.0, dollars) == "$0.00");
    CHECK(format_money(999.999, dollars) == "$1,000.00");
    CHECK(format_money(1234567.0, dollars) == "$1,234,567.00");
    CHECK(format_money(-12.0, dollars) == "−$12.00");
    CHECK(format_money(-0.001, dollars) == "$0.00");  // rounds to nothing: not negative
    CHECK(format_money(std::numeric_limits<double>::quiet_NaN(), dollars) == "--");
}

TEST_CASE("other ways of writing money") {
    Currency euros;
    euros.symbol = "€";
    euros.symbol_after = true;
    euros.decimal_comma = true;
    CHECK(format_money(1234.5, euros) == "1.234,50 €");

    Currency francs;
    francs.symbol = "CHF";
    CHECK(format_money(12.0, francs) == "CHF 12.00");

    Currency yen;
    yen.symbol = "¥";
    yen.decimals = 0;
    CHECK(format_money(1234.4, yen) == "¥1,234");
}
