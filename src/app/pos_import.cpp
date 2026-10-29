#include "app/pos_import.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <initializer_list>
#include <system_error>
#include <utility>

namespace ninkasi::app {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// The three numbers of a date at the start of `text` ("2026-10-28T...",
// "10/28/26 5:45"), and how many digits the first has.
struct DateParts {
    std::array<int, 3> numbers{};
    std::size_t first_digits = 0;
};

std::optional<DateParts> date_parts(std::string_view text) {
    text = trim(text);
    DateParts parts;
    std::size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t start = at;
        while (at < text.size() && is_digit(text[at])) ++at;
        if (at == start || at - start > 4) return std::nullopt;
        if (i == 0) parts.first_digits = at - start;
        std::from_chars(text.data() + start, text.data() + at, parts.numbers[static_cast<std::size_t>(i)]);
        if (i < 2) {
            if (at >= text.size() || (text[at] != '-' && text[at] != '/' && text[at] != '.')) return std::nullopt;
            ++at;
        }
    }
    // Whatever follows is a time, after a space or a T.
    if (at < text.size() && text[at] != ' ' && text[at] != 'T' && text[at] != 't') return std::nullopt;
    return parts;
}

std::string_view order_key(DateOrder order) {
    switch (order) {
    case DateOrder::YearMonthDay: return "ymd";
    case DateOrder::MonthDayYear: return "mdy";
    case DateOrder::DayMonthYear: return "dmy";
    }
    return "mdy";
}

// How strongly a header name suggests a column: the best word wins.
int score(std::string_view name, std::initializer_list<std::pair<std::string_view, int>> words) {
    int best = 0;
    for (const auto& [word, value] : words) {
        if (name.find(word) != std::string_view::npos) best = std::max(best, value);
    }
    return best;
}

} // namespace

std::optional<domain::Date> parse_pos_date(std::string_view text, DateOrder order) {
    const std::optional<DateParts> parts = date_parts(text);
    if (!parts) return std::nullopt;
    const auto [a, b, c] = parts->numbers;
    domain::Date date;
    if (parts->first_digits == 4 || order == DateOrder::YearMonthDay) {
        date = {a, b, c};
    } else if (order == DateOrder::MonthDayYear) {
        date = {c, a, b};
    } else {
        date = {c, b, a};
    }
    if (date.year < 100) date.year += 2000;
    if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > domain::days_in_month(date.year, date.month)) {
        return std::nullopt;
    }
    return date;
}

std::optional<double> parse_pos_amount(std::string_view text) {
    text = trim(text);
    bool negative = false;
    std::string digits;      // with '.' for the decimal point
    std::string separators;  // the separators met, in order
    std::size_t last_separator = std::string::npos;
    for (char c : text) {
        if (c == '-' || c == '(') negative = true;
        if (is_digit(c)) {
            digits += c;
        } else if (c == '.' || c == ',') {
            separators += c;
            last_separator = digits.size();
            digits += c;
        }
    }
    if (std::none_of(digits.begin(), digits.end(), is_digit)) return std::nullopt;

    // The decimal point is the last separator if one or two digits follow it
    // and no separator of its kind came before it ("1,234" is a thousand).
    std::string number;
    const bool has_decimal = last_separator != std::string::npos && digits.size() - last_separator - 1 >= 1 &&
                             digits.size() - last_separator - 1 <= 2 &&
                             std::count(separators.begin(), separators.end(), digits[last_separator]) == 1;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (is_digit(digits[i])) number += digits[i];
        else if (has_decimal && i == last_separator) number += '.';
    }
    const double value = std::strtod(number.c_str(), nullptr);
    return negative ? -value : value;
}

PosColumns guess_pos_columns(const std::vector<std::vector<std::string>>& table) {
    PosColumns columns;
    if (table.empty()) return columns;
    const std::vector<std::string>& header = table.front();
    std::array<int, 4> best{};  // date, item, quantity, amount
    for (std::size_t i = 0; i < header.size(); ++i) {
        const std::string name = lower(header[i]);
        const int column = static_cast<int>(i);
        const std::array<int, 4> scores = {
            score(name, {{"date", 3}, {"day", 2}, {"time", 1}}),
            score(name, {{"item", 3}, {"product", 3}, {"menu", 2}, {"name", 2}, {"description", 1}}),
            score(name, {{"qty", 3}, {"quantity", 3}, {"count", 2}, {"units", 2}, {"sold", 1}}),
            score(name, {{"net", 4}, {"amount", 3}, {"total", 2}, {"sales", 2}, {"revenue", 2}, {"gross", 1},
                         {"price", 1}}),
        };
        int* fields[] = {&columns.date, &columns.item, &columns.quantity, &columns.amount};
        for (std::size_t f = 0; f < 4; ++f) {
            if (scores[f] > best[f]) {
                best[f] = scores[f];
                *fields[f] = column;
            }
        }
    }
    // A column claimed twice ("Item sold count"?) goes to the closer match.
    if (columns.quantity == columns.item) columns.quantity = -1;
    if (columns.amount == columns.quantity) columns.quantity = -1;

    // Dates: a first number over 12 is a day; a second over 12, so is that.
    if (columns.date >= 0) {
        for (std::size_t r = 1; r < table.size(); ++r) {
            if (static_cast<std::size_t>(columns.date) >= table[r].size()) continue;
            const std::optional<DateParts> parts = date_parts(table[r][static_cast<std::size_t>(columns.date)]);
            if (!parts) continue;
            if (parts->first_digits == 4) {
                columns.dates = DateOrder::YearMonthDay;
                break;
            }
            if (parts->numbers[0] > 12) {
                columns.dates = DateOrder::DayMonthYear;
                break;
            }
            if (parts->numbers[1] > 12) {
                columns.dates = DateOrder::MonthDayYear;
                break;
            }
        }
    }
    return columns;
}

std::string header_signature(const std::vector<std::string>& header) {
    std::string signature;
    for (const std::string& name : header) {
        if (!signature.empty()) signature += '|';
        signature += lower(trim(name));
    }
    return signature;
}

std::string to_text(const PosColumns& columns) {
    return "date=" + std::to_string(columns.date) + ";item=" + std::to_string(columns.item) +
           ";quantity=" + std::to_string(columns.quantity) + ";amount=" + std::to_string(columns.amount) +
           ";dates=" + std::string(order_key(columns.dates));
}

std::optional<PosColumns> pos_columns_from_text(std::string_view text) {
    PosColumns columns;
    while (!text.empty()) {
        const std::size_t end = std::min(text.find(';'), text.size());
        const std::string_view pair = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        const std::size_t equals = pair.find('=');
        if (equals == std::string_view::npos) return std::nullopt;
        const std::string_view name = pair.substr(0, equals);
        const std::string_view value = pair.substr(equals + 1);
        if (name == "dates") {
            if (value == "ymd") columns.dates = DateOrder::YearMonthDay;
            else if (value == "dmy") columns.dates = DateOrder::DayMonthYear;
            else columns.dates = DateOrder::MonthDayYear;
            continue;
        }
        int number = -1;
        if (std::from_chars(value.data(), value.data() + value.size(), number).ec != std::errc{}) return std::nullopt;
        if (name == "date") columns.date = number;
        else if (name == "item") columns.item = number;
        else if (name == "quantity") columns.quantity = number;
        else if (name == "amount") columns.amount = number;
    }
    return columns;
}

PosTable read_pos_rows(const std::vector<std::vector<std::string>>& table, const PosColumns& columns) {
    PosTable result;
    const auto cell = [](const std::vector<std::string>& row, int column) -> std::optional<std::string_view> {
        if (column < 0 || static_cast<std::size_t>(column) >= row.size()) return std::nullopt;
        return row[static_cast<std::size_t>(column)];
    };
    for (std::size_t r = 1; r < table.size(); ++r) {
        const std::vector<std::string>& row = table[r];
        const std::optional<std::string_view> date = cell(row, columns.date);
        const std::optional<std::string_view> item = cell(row, columns.item);
        const std::optional<std::string_view> amount = cell(row, columns.amount);
        std::optional<domain::Date> day = date ? parse_pos_date(*date, columns.dates) : std::nullopt;
        std::optional<double> taken = amount ? parse_pos_amount(*amount) : std::nullopt;
        std::optional<double> quantity = 1.0;
        if (columns.quantity >= 0) {
            const std::optional<std::string_view> text = cell(row, columns.quantity);
            quantity = text ? parse_pos_amount(*text) : std::nullopt;
        }
        if (!item || trim(*item).empty() || !day || !taken || !quantity) {
            ++result.skipped;
            continue;
        }
        result.rows.push_back({*day, std::string(trim(*item)), *quantity, *taken});
    }
    return result;
}

} // namespace ninkasi::app
