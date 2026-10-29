#include "app/expense_service.h"

#include "data/audit_log.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace ninkasi::app {

namespace {

constexpr const char* kAction = "see or change expenses";

[[noreturn]] void fail(const std::string& message) {
    throw std::invalid_argument(message);
}

// Whether two names are the same, ignoring case.
bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

} // namespace

ExpenseService::ExpenseService(data::Database& db, Session session)
    : db_(db), session_(std::move(session)), repository_(db) {
    reload();
}

void ExpenseService::reload() {
    if (allowed()) {
        categories_ = repository_.categories();
        recurring_ = repository_.recurring();
    }
    ++revision_;
}

// --- Categories -------------------------------------------------------------------------

const domain::ExpenseCategory* ExpenseService::find_category(std::int64_t id) const {
    const auto found = std::find_if(categories_.begin(), categories_.end(),
                                    [id](const domain::ExpenseCategory& category) { return category.id == id; });
    return found == categories_.end() ? nullptr : &*found;
}

std::int64_t ExpenseService::add_category(std::string_view name) {
    require_admin(session_, kAction);
    domain::ExpenseCategory category{0, trimmed(name), true};
    if (category.name.empty()) fail("A category needs a name.");
    for (const domain::ExpenseCategory& other : categories_) {
        if (same_name(other.name, category.name)) fail("There's a category called that already.");
    }
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(category);
    data::record_audit(db_, session_.user_id, "expense_category.create", "expense_category", id, category.name);
    tx.commit();
    reload();
    return id;
}

void ExpenseService::update_category(const domain::ExpenseCategory& category) {
    require_admin(session_, kAction);
    domain::ExpenseCategory updated = category;
    updated.name = trimmed(category.name);
    if (updated.name.empty()) fail("A category needs a name.");
    check_category(updated.id);
    for (const domain::ExpenseCategory& other : categories_) {
        if (other.id != updated.id && same_name(other.name, updated.name)) {
            fail("There's a category called that already.");
        }
    }
    data::Transaction tx(db_);
    repository_.update(updated);
    data::record_audit(db_, session_.user_id, "expense_category.update", "expense_category", updated.id, updated.name);
    tx.commit();
    reload();
}

void ExpenseService::check_category(std::int64_t id) const {
    if (!find_category(id)) fail("Choose a category.");
}

// --- Expenses ---------------------------------------------------------------------------

std::vector<data::ExpenseRow> ExpenseService::expenses(domain::Date from, domain::Date to) {
    require_admin(session_, kAction);
    return repository_.expenses(from, to);
}

void ExpenseService::check(const domain::Expense& expense) const {
    if (!(expense.amount >= 0.0)) fail("An expense can't be negative.");
    check_category(expense.category_id);
}

std::int64_t ExpenseService::add_expense(const domain::Expense& expense) {
    require_admin(session_, kAction);
    check(expense);
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(expense, session_.user_id);
    data::record_audit(db_, session_.user_id, "expense.create", "expense", id,
                       expense.payee + ": " + std::to_string(expense.amount));
    tx.commit();
    reload();
    return id;
}

void ExpenseService::update_expense(const domain::Expense& expense) {
    require_admin(session_, kAction);
    check(expense);
    data::Transaction tx(db_);
    repository_.update(expense);
    data::record_audit(db_, session_.user_id, "expense.update", "expense", expense.id,
                       expense.payee + ": " + std::to_string(expense.amount));
    tx.commit();
    reload();
}

void ExpenseService::remove_expense(std::int64_t id) {
    require_admin(session_, kAction);
    data::Transaction tx(db_);
    repository_.remove_expense(id);
    data::record_audit(db_, session_.user_id, "expense.delete", "expense", id, "");
    tx.commit();
    reload();
}

// --- Recurring expenses -------------------------------------------------------------------

void ExpenseService::check(const domain::RecurringExpense& expense) const {
    if (trimmed(expense.description).empty()) fail("Say what the expense is.");
    if (!(expense.monthly >= 0.0)) fail("An expense can't be negative.");
    if (expense.last && *expense.last < expense.first) fail("Its last month is before its first.");
    check_category(expense.category_id);
}

std::int64_t ExpenseService::add_recurring(const domain::RecurringExpense& expense) {
    require_admin(session_, kAction);
    check(expense);
    domain::RecurringExpense added = expense;
    added.description = trimmed(expense.description);
    data::Transaction tx(db_);
    const std::int64_t id = repository_.insert(added, session_.user_id);
    data::record_audit(db_, session_.user_id, "recurring_expense.create", "recurring_expense", id,
                       added.description + ": " + std::to_string(added.monthly));
    tx.commit();
    reload();
    return id;
}

void ExpenseService::update_recurring(const domain::RecurringExpense& expense) {
    require_admin(session_, kAction);
    check(expense);
    domain::RecurringExpense updated = expense;
    updated.description = trimmed(expense.description);
    data::Transaction tx(db_);
    repository_.update(updated);
    data::record_audit(db_, session_.user_id, "recurring_expense.update", "recurring_expense", updated.id,
                       updated.description + ": " + std::to_string(updated.monthly));
    tx.commit();
    reload();
}

void ExpenseService::remove_recurring(std::int64_t id) {
    require_admin(session_, kAction);
    data::Transaction tx(db_);
    repository_.remove_recurring(id);
    data::record_audit(db_, session_.user_id, "recurring_expense.delete", "recurring_expense", id, "");
    tx.commit();
    reload();
}

std::vector<domain::ExpenseLine> ExpenseService::lines_between(domain::Date from, domain::Date to) {
    require_admin(session_, kAction);
    std::vector<domain::Expense> dated;
    for (data::ExpenseRow& row : repository_.expenses(from, to)) dated.push_back(std::move(row.expense));
    return domain::expenses_between(dated, recurring_, from, to);
}

} // namespace ninkasi::app
