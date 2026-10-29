#include "domain/expenses.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <system_error>

namespace ninkasi::domain {

namespace {

constexpr const char* kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};

} // namespace

Month month_of(Date date) {
    return {date.year, date.month};
}

Month add_months(Month month, int months) {
    const int index = month.year * 12 + (month.month - 1) + months;
    const int year = index >= 0 ? index / 12 : (index - 11) / 12;
    return {year, index - year * 12 + 1};
}

Date first_day(Month month) {
    return {month.year, month.month, 1};
}

Date last_day(Month month) {
    return {month.year, month.month, days_in_month(month.year, month.month)};
}

std::string to_text(Month month) {
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d", month.year, month.month);
    return text;
}

std::optional<Month> parse_month(std::string_view text) {
    if (text.size() != 7 || text[4] != '-') return std::nullopt;
    Month month;
    if (std::from_chars(text.data(), text.data() + 4, month.year).ec != std::errc{}) return std::nullopt;
    if (std::from_chars(text.data() + 5, text.data() + 7, month.month).ec != std::errc{}) return std::nullopt;
    if (month.month < 1 || month.month > 12) return std::nullopt;
    return month;
}

std::string month_name(Month month) {
    return std::string(kMonthNames[std::clamp(month.month, 1, 12) - 1]) + " " + std::to_string(month.year);
}

std::vector<ExpenseLine> expenses_between(std::span<const Expense> expenses,
                                          std::span<const RecurringExpense> recurring, Date from, Date to) {
    std::vector<ExpenseLine> lines;
    for (const Expense& expense : expenses) {
        if (expense.day < from || expense.day > to) continue;
        lines.push_back({expense.day, expense.category_id, expense.payee, expense.amount, false});
    }
    for (const RecurringExpense& entry : recurring) {
        // Each month it runs whose first day falls in the period.
        Month month = std::max(entry.first, month_of(from));
        if (first_day(month) < from) month = add_months(month, 1);
        for (; first_day(month) <= to && (!entry.last || month <= *entry.last); month = add_months(month, 1)) {
            lines.push_back({first_day(month), entry.category_id, entry.description, entry.monthly, true});
        }
    }
    std::stable_sort(lines.begin(), lines.end(), [](const ExpenseLine& a, const ExpenseLine& b) { return a.day < b.day; });
    return lines;
}

} // namespace ninkasi::domain
