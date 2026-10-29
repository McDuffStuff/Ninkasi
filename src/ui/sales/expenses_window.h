#pragma once
// Sales > Expenses (admins): what the brewery spends besides what it brews
// with. Three tabs: a month's expenses, the ones that recur every month
// (rent, wages...) and the categories they're kept in. Every field is saved
// when it's finished.

#include "app/expense_service.h"
#include "app/sales_service.h"
#include "domain/expenses.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ninkasi::ui {

class ExpensesWindow {
public:
    ExpensesWindow(app::ExpenseService& expenses, const app::SalesService& sales, float ui_scale);

    void show();
    void draw();

private:
    void reload();
    void draw_month();
    void draw_recurring();
    void draw_categories();
    // A dropdown of the categories in use (and `category_id`'s, even if not).
    bool category_combo(const char* label, std::int64_t& category_id);
    // The category for something new: Other, or the first in use.
    std::int64_t default_category() const;
    // Runs a change, showing what was wrong if it's refused.
    template <typename Change>
    void save(Change change);

    app::ExpenseService& expenses_;
    const app::SalesService& sales_;  // for the currency
    float ui_scale_;

    bool visible_ = false;
    bool focus_ = false;
    std::string error_;
    domain::Month month_{};  // shown on the first tab
    std::vector<data::ExpenseRow> rows_;                // its expenses: working copies
    std::vector<domain::ExpenseLine> recurring_lines_;  // and the recurring ones in it
    std::vector<domain::RecurringExpense> recurring_;   // working copies
    std::vector<domain::ExpenseCategory> categories_;   // working copies
    std::uint64_t revision_ = ~0ull;
    domain::Month loaded_month_{};
    std::int64_t payee_to_focus_ = 0;  // a just-added expense
};

} // namespace ninkasi::ui
