#pragma once
// What the brewery spends besides what it brews with: rent, wages,
// utilities, repairs... (Ingredients and packaging come in on inventory
// invoices, and are costed into the beer instead; see domain/costing.h.)
//
// An expense is paid on a day. A recurring one, such as rent or a salary,
// is the same amount every month from its first month to its last (or for
// as long as it goes on), and counts on the first of each month.

#include "domain/timestamp.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ninkasi::domain {

struct ExpenseCategory {
    std::int64_t id = 0;
    std::string name;
    bool active = true;  // offered for new expenses

    bool operator==(const ExpenseCategory&) const = default;
};

struct Expense {
    std::int64_t id = 0;
    Date day{};
    std::string payee;
    std::int64_t category_id = 0;
    double amount = 0.0;
    std::string number;  // invoice or receipt number
    std::string note;

    bool operator==(const Expense&) const = default;
};

// A calendar month.
struct Month {
    int year = 1970;
    int month = 1;  // 1-12

    auto operator<=>(const Month&) const = default;
};
Month month_of(Date date);
Month add_months(Month month, int months);
Date first_day(Month month);
Date last_day(Month month);
// "2026-10", as stored, and back (nullopt if malformed).
std::string to_text(Month month);
std::optional<Month> parse_month(std::string_view text);
// "October 2026".
std::string month_name(Month month);

struct RecurringExpense {
    std::int64_t id = 0;
    std::int64_t category_id = 0;
    std::string description;
    double monthly = 0.0;
    Month first{};
    std::optional<Month> last;  // none: still going

    bool operator==(const RecurringExpense&) const = default;
};

// One expense in a period: a dated one, or a month of a recurring one.
struct ExpenseLine {
    Date day{};
    std::int64_t category_id = 0;
    std::string description;  // the payee, or what recurs
    double amount = 0.0;
    bool recurring = false;
};

// The expenses from `from` to `to` (both days included), by day: the dated
// ones, and each recurring one for every month whose first day is in them.
std::vector<ExpenseLine> expenses_between(std::span<const Expense> expenses,
                                          std::span<const RecurringExpense> recurring, Date from, Date to);

} // namespace ninkasi::domain
