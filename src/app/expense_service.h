#pragma once
// The brewery's expenses: dated ones, those that recur every month, and the
// categories they're kept in. They're the brewery's money, so only admins
// see or change them; for anyone else every call throws NotAllowed (and
// categories() and recurring() are empty).

#include "app/session.h"
#include "data/database.h"
#include "data/expense_repository.h"
#include "domain/expenses.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace ninkasi::app {

class ExpenseService {
public:
    ExpenseService(data::Database& db, Session session);

    bool allowed() const { return session_.is_admin(); }
    // Grows with every change, so windows know when to read again.
    std::uint64_t revision() const { return revision_; }

    // --- Categories -------------------------------------------------------------------
    const std::vector<domain::ExpenseCategory>& categories() const { return categories_; }
    const domain::ExpenseCategory* find_category(std::int64_t id) const;
    // Each throws std::invalid_argument for a blank name, or one another category has.
    std::int64_t add_category(std::string_view name);
    void update_category(const domain::ExpenseCategory& category);

    // --- Expenses ---------------------------------------------------------------------
    // From `from` to `to`, both included, newest first.
    std::vector<data::ExpenseRow> expenses(domain::Date from, domain::Date to);
    // Each throws std::invalid_argument for a negative amount or an unknown category.
    std::int64_t add_expense(const domain::Expense& expense);
    void update_expense(const domain::Expense& expense);
    void remove_expense(std::int64_t id);

    // --- Recurring expenses -------------------------------------------------------------
    const std::vector<domain::RecurringExpense>& recurring() const { return recurring_; }
    // Each throws std::invalid_argument for a blank description, a negative
    // amount, an unknown category, or a last month before the first.
    std::int64_t add_recurring(const domain::RecurringExpense& expense);
    void update_recurring(const domain::RecurringExpense& expense);
    void remove_recurring(std::int64_t id);

    // Every expense from `from` to `to`, recurring ones included (see
    // domain::expenses_between).
    std::vector<domain::ExpenseLine> lines_between(domain::Date from, domain::Date to);

private:
    void reload();
    void check_category(std::int64_t id) const;
    void check(const domain::Expense& expense) const;
    void check(const domain::RecurringExpense& expense) const;

    data::Database& db_;
    Session session_;
    data::ExpenseRepository repository_;
    std::vector<domain::ExpenseCategory> categories_;
    std::vector<domain::RecurringExpense> recurring_;
    std::uint64_t revision_ = 0;
};

} // namespace ninkasi::app
