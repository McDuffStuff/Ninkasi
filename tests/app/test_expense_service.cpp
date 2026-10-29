#include "app/expense_service.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <stdexcept>

using namespace ninkasi;

namespace {

struct Office {
    data::Database db{":memory:"};
    app::Session admin;

    Office() {
        data::migrate(db);
        admin = app::open_local_session(db);
    }

    app::Session brewer() {
        app::UserService users(db, app::PasswordCost{1024, 1});
        users.set_password(admin, admin.user_id, "admin");
        return *users.sign_in(users.add(admin, "Sam", domain::Role::Brewer, ""), "");
    }

    std::int64_t category(const app::ExpenseService& expenses, std::string_view name) {
        for (const domain::ExpenseCategory& category : expenses.categories()) {
            if (category.name == name) return category.id;
        }
        FAIL("no category " << name);
        return 0;
    }
};

} // namespace

TEST_CASE("expense categories start with the usual ones and can be added to") {
    Office office;
    app::ExpenseService expenses(office.db, office.admin);
    CHECK(expenses.categories().size() >= 8);
    CHECK(office.category(expenses, "Rent") > 0);
    CHECK_THROWS_AS(expenses.add_category(" rent "), std::invalid_argument);
    const std::int64_t id = expenses.add_category("Taxes and fees");
    domain::ExpenseCategory retired = *expenses.find_category(id);
    retired.active = false;
    expenses.update_category(retired);
    CHECK_FALSE(expenses.find_category(id)->active);
}

TEST_CASE("expenses are recorded, changed and removed by admins only") {
    Office office;
    app::ExpenseService expenses(office.db, office.admin);
    const std::int64_t repairs = office.category(expenses, "Repairs and maintenance");
    domain::Expense plumber{0, {2026, 10, 3}, "Ace Plumbing", repairs, 250.0, "INV-77", "Glycol leak"};
    plumber.id = expenses.add_expense(plumber);
    plumber.amount = 275.0;
    expenses.update_expense(plumber);
    const std::vector<data::ExpenseRow> october = expenses.expenses({2026, 10, 1}, {2026, 10, 31});
    REQUIRE(october.size() == 1);
    CHECK(october.front().expense == plumber);

    CHECK_THROWS_AS(expenses.add_expense({0, {2026, 10, 3}, "", repairs, -1.0, "", ""}), std::invalid_argument);
    CHECK_THROWS_AS(expenses.add_expense({0, {2026, 10, 3}, "", 999, 1.0, "", ""}), std::invalid_argument);

    const std::int64_t rent = office.category(expenses, "Rent");
    expenses.add_recurring({0, rent, "Rent", 3000.0, {2026, 9}, std::nullopt});
    CHECK_THROWS_AS(expenses.add_recurring({0, rent, " ", 10.0, {2026, 9}, std::nullopt}), std::invalid_argument);
    CHECK_THROWS_AS(expenses.add_recurring({0, rent, "Lease", 10.0, {2026, 9}, domain::Month{2026, 8}}),
                    std::invalid_argument);
    double total = 0.0;
    for (const domain::ExpenseLine& line : expenses.lines_between({2026, 10, 1}, {2026, 10, 31})) total += line.amount;
    CHECK(total == doctest::Approx(3275.0));

    expenses.remove_expense(plumber.id);
    CHECK(expenses.expenses({2026, 10, 1}, {2026, 10, 31}).empty());

    app::ExpenseService brewer(office.db, office.brewer());
    CHECK_FALSE(brewer.allowed());
    CHECK(brewer.categories().empty());
    CHECK_THROWS_AS(brewer.expenses({2026, 10, 1}, {2026, 10, 31}), app::NotAllowed);
    CHECK_THROWS_AS(brewer.add_expense(plumber), app::NotAllowed);
    CHECK_THROWS_AS(brewer.lines_between({2026, 10, 1}, {2026, 10, 31}), app::NotAllowed);
}
