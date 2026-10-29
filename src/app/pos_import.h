#pragma once
// Reading a point-of-sale export: a CSV file with a row for each item sold
// (or each item's total for a day), whatever system wrote it. The brewer
// says which column holds what; Ninkasi guesses from the header, and
// remembers the answer for files with the same header.

#include "domain/sales.h"
#include "domain/timestamp.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::app {

// How the dates in an export are written, when it isn't year first.
enum class DateOrder { YearMonthDay, MonthDayYear, DayMonthYear };

// Which column (from 0) holds what.
struct PosColumns {
    int date = -1;
    int item = -1;
    int quantity = -1;  // -1: each row is one sold
    int amount = -1;    // what was taken, net of discounts
    DateOrder dates = DateOrder::MonthDayYear;

    bool complete() const { return date >= 0 && item >= 0 && amount >= 0; }
    bool operator==(const PosColumns&) const = default;
};

// The date at the start of `text`, whatever follows it ("2026-10-28",
// "10/28/2026 5:45 PM", "28.10.26"). A year first is always read as one;
// otherwise `order` says which comes first. Two-digit years are this
// century's. nullopt if it isn't a date.
std::optional<domain::Date> parse_pos_date(std::string_view text, DateOrder order);

// An amount as exports write them: "$1,234.50", "1.234,50 €", "-12.00" or
// "(12.00)" for a refund. A separator followed by one or two digits at the
// end is the decimal point; others separate thousands. nullopt if there's
// no number.
std::optional<double> parse_pos_amount(std::string_view text);

// The columns, guessed from the header's names ("Date", "Item", "Qty",
// "Net sales"...), with the date order guessed from the dates in `table`
// (its header first).
PosColumns guess_pos_columns(const std::vector<std::vector<std::string>>& table);

// A key for files with this header, to remember their columns by.
std::string header_signature(const std::vector<std::string>& header);
// The columns as text, to keep in settings, and back.
std::string to_text(const PosColumns& columns);
std::optional<PosColumns> pos_columns_from_text(std::string_view text);

// The rows of an export (its header first, then a row per line). Rows with
// no item, or whose date, quantity or amount can't be read (a total line,
// say), are left out and counted.
struct PosTable {
    std::vector<domain::PosRow> rows;
    int skipped = 0;
};
PosTable read_pos_rows(const std::vector<std::vector<std::string>>& table, const PosColumns& columns);

} // namespace ninkasi::app
