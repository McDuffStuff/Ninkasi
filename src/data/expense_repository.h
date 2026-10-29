#pragma once
// Reads and writes expenses, their categories and recurring expenses.
// ExpenseService decides what may be written; this only stores it.

#include "data/database.h"
#include "domain/expenses.h"

#include <cstdint>
#include <vector>

namespace ninkasi::data {

// An expense with who recorded it.
struct ExpenseRow {
    domain::Expense expense;
    std::string user;
};

class ExpenseRepository {
public:
    explicit ExpenseRepository(Database& db) : db_(db) {}

    // By name.
    std::vector<domain::ExpenseCategory> categories();
    std::int64_t insert(const domain::ExpenseCategory& category);
    void update(const domain::ExpenseCategory& category);

    // From `from` to `to`, both included, newest first.
    std::vector<ExpenseRow> expenses(domain::Date from, domain::Date to);
    std::int64_t insert(const domain::Expense& expense, std::int64_t user_id);
    void update(const domain::Expense& expense);
    void remove_expense(std::int64_t id);

    // By first month.
    std::vector<domain::RecurringExpense> recurring();
    std::int64_t insert(const domain::RecurringExpense& expense, std::int64_t user_id);
    void update(const domain::RecurringExpense& expense);
    void remove_recurring(std::int64_t id);

private:
    Database& db_;
};

} // namespace ninkasi::data
