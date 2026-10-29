#include "app/csv.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ninkasi;
using Rows = std::vector<std::vector<std::string>>;

TEST_CASE("CSV is read field by field, quotes and all") {
    CHECK(app::parse_csv("Date,Item,Qty\r\n2026-10-01,Red Ale,3\r\n") ==
          Rows{{"Date", "Item", "Qty"}, {"2026-10-01", "Red Ale", "3"}});
    // Quoted fields hold commas, quotes and line breaks.
    CHECK(app::parse_csv("\"Pint, 16 oz\",\"Say \"\"hi\"\"\",\"two\nlines\"\n") ==
          Rows{{"Pint, 16 oz", "Say \"hi\"", "two\nlines"}});
    // Empty fields are kept; blank lines and a byte order mark are not.
    CHECK(app::parse_csv("\xEF\xBB\xBF" "a,,c\n\n\r\n,\nlast") == Rows{{"a", "", "c"}, {"", ""}, {"last"}});
    CHECK(app::parse_csv("").empty());
}

TEST_CASE("CSV written by Ninkasi reads back the same") {
    const std::vector<std::string> fields = {"plain", "with, comma", "with \"quotes\"", "line\r\nbreak", ""};
    CHECK(app::parse_csv(app::csv_row(fields)) == Rows{fields});
}

TEST_CASE("the separator is guessed from the first line") {
    CHECK(app::guess_csv_separator("Date,Item,Qty\n") == ',');
    CHECK(app::guess_csv_separator("Datum;Artikel;\"Menge, Stk\"\n") == ';');
    CHECK(app::guess_csv_separator("Date\tItem\tQty\n") == '\t');
    CHECK(app::parse_csv("a;b\n1;2\n", ';') == Rows{{"a", "b"}, {"1", "2"}});
}
