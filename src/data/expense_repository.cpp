#include "data/expense_repository.h"

namespace ninkasi::data {

namespace {

// Binds a recurring expense's fields to parameters 1 to 5.
void bind_recurring(Statement& stmt, const domain::RecurringExpense& expense) {
    stmt.bind(1, expense.category_id)
        .bind(2, expense.description)
        .bind(3, expense.monthly)
        .bind(4, domain::to_text(expense.first));
    if (expense.last) {
        stmt.bind(5, domain::to_text(*expense.last));
    } else {
        stmt.bind_null(5);
    }
}

// Binds an expense's fields to parameters 1 to 6.
void bind_expense(Statement& stmt, const domain::Expense& expense) {
    stmt.bind(1, domain::to_iso8601(expense.day))
        .bind(2, expense.payee)
        .bind(3, expense.category_id)
        .bind(4, expense.amount)
        .bind(5, expense.number)
        .bind(6, expense.note);
}

} // namespace

std::vector<domain::ExpenseCategory> ExpenseRepository::categories() {
    std::vector<domain::ExpenseCategory> categories;
    auto stmt = db_.prepare("SELECT id, name, active FROM expense_categories ORDER BY name COLLATE NOCASE;");
    while (stmt.step()) {
        categories.push_back({stmt.column_int64(0), stmt.column_text(1), stmt.column_int64(2) != 0});
    }
    return categories;
}

std::int64_t ExpenseRepository::insert(const domain::ExpenseCategory& category) {
    auto stmt = db_.prepare("INSERT INTO expense_categories (name, active) VALUES (?, ?);");
    stmt.bind(1, category.name).bind(2, category.active ? 1 : 0);
    stmt.execute();
    return db_.last_insert_id();
}

void ExpenseRepository::update(const domain::ExpenseCategory& category) {
    auto stmt = db_.prepare("UPDATE expense_categories SET name = ?, active = ? WHERE id = ?;");
    stmt.bind(1, category.name).bind(2, category.active ? 1 : 0).bind(3, category.id);
    stmt.execute();
}

std::vector<ExpenseRow> ExpenseRepository::expenses(domain::Date from, domain::Date to) {
    std::vector<ExpenseRow> rows;
    auto stmt = db_.prepare(
        "SELECT e.id, e.day, e.payee, e.category_id, e.amount, e.number, e.note, u.display_name "
        "FROM expenses e JOIN users u ON u.id = e.user_id WHERE e.day BETWEEN ? AND ? ORDER BY e.day DESC, e.id DESC;");
    stmt.bind(1, domain::to_iso8601(from)).bind(2, domain::to_iso8601(to));
    while (stmt.step()) {
        ExpenseRow row;
        domain::Expense& expense = row.expense;
        expense.id = stmt.column_int64(0);
        expense.day = domain::parse_date(stmt.column_text(1)).value_or(domain::Date{});
        expense.payee = stmt.column_text(2);
        expense.category_id = stmt.column_int64(3);
        expense.amount = stmt.column_double(4);
        expense.number = stmt.column_text(5);
        expense.note = stmt.column_text(6);
        row.user = stmt.column_text(7);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::int64_t ExpenseRepository::insert(const domain::Expense& expense, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO expenses (day, payee, category_id, amount, number, note, user_id) VALUES (?, ?, ?, ?, ?, ?, ?);");
    bind_expense(stmt, expense);
    stmt.bind(7, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

void ExpenseRepository::update(const domain::Expense& expense) {
    auto stmt = db_.prepare(
        "UPDATE expenses SET day = ?, payee = ?, category_id = ?, amount = ?, number = ?, note = ? WHERE id = ?;");
    bind_expense(stmt, expense);
    stmt.bind(7, expense.id);
    stmt.execute();
}

void ExpenseRepository::remove_expense(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM expenses WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

std::vector<domain::RecurringExpense> ExpenseRepository::recurring() {
    std::vector<domain::RecurringExpense> expenses;
    auto stmt = db_.prepare(
        "SELECT id, category_id, description, monthly, first_month, last_month FROM recurring_expenses "
        "ORDER BY first_month, id;");
    while (stmt.step()) {
        domain::RecurringExpense expense;
        expense.id = stmt.column_int64(0);
        expense.category_id = stmt.column_int64(1);
        expense.description = stmt.column_text(2);
        expense.monthly = stmt.column_double(3);
        expense.first = domain::parse_month(stmt.column_text(4)).value_or(domain::Month{});
        if (!stmt.column_is_null(5)) expense.last = domain::parse_month(stmt.column_text(5));
        expenses.push_back(std::move(expense));
    }
    return expenses;
}

std::int64_t ExpenseRepository::insert(const domain::RecurringExpense& expense, std::int64_t user_id) {
    auto stmt = db_.prepare(
        "INSERT INTO recurring_expenses (category_id, description, monthly, first_month, last_month, user_id) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    bind_recurring(stmt, expense);
    stmt.bind(6, user_id);
    stmt.execute();
    return db_.last_insert_id();
}

void ExpenseRepository::update(const domain::RecurringExpense& expense) {
    auto stmt = db_.prepare(
        "UPDATE recurring_expenses SET category_id = ?, description = ?, monthly = ?, first_month = ?, "
        "last_month = ? WHERE id = ?;");
    bind_recurring(stmt, expense);
    stmt.bind(6, expense.id);
    stmt.execute();
}

void ExpenseRepository::remove_recurring(std::int64_t id) {
    auto stmt = db_.prepare("DELETE FROM recurring_expenses WHERE id = ?;");
    stmt.bind(1, id);
    stmt.execute();
}

} // namespace ninkasi::data
