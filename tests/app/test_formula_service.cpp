#include "app/formula_service.h"
#include "app/session.h"
#include "app/user_service.h"
#include "calc/alcohol.h"
#include "calc/color.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <stdexcept>

using namespace ninkasi;
using calc::Formula;

namespace {

struct Brewery {
    data::Database db{":memory:"};
    app::Session admin;

    Brewery() {
        calc::clear_adjusted_formulas();
        data::migrate(db);
        admin = app::open_local_session(db);
    }
    // Adjusted formulas are shared by every calculation; leave none behind.
    ~Brewery() { calc::clear_adjusted_formulas(); }
};

} // namespace

TEST_CASE("an adjusted formula is saved and used from then on") {
    Brewery brewery;
    app::FormulaService formulas(brewery.db, brewery.admin);
    CHECK_FALSE(formulas.any_adjusted());

    formulas.adjust(Formula::AbvSimple, "  (OG - FG) \\times 135  ");
    CHECK(calc::abv(1.050, 1.010) == near(0.054, 1e-12));
    const std::optional<app::FormulaAdjustment> adjustment = formulas.adjustment(Formula::AbvSimple);
    REQUIRE(adjustment);
    CHECK(adjustment->latex == "(OG - FG) \\times 135");
    CHECK(adjustment->by == "Local Brewer");
    CHECK(formulas.any_adjusted());

    // Ninkasi starting again finds it.
    calc::clear_adjusted_formulas();
    app::FormulaService again(brewery.db, brewery.admin);
    CHECK(again.adjustment(Formula::AbvSimple));
    CHECK(calc::abv(1.050, 1.010) == near(0.054, 1e-12));
}

TEST_CASE("formulas can be put back one at a time or all together") {
    Brewery brewery;
    app::FormulaService formulas(brewery.db, brewery.admin);
    formulas.adjust(Formula::AbvSimple, "(OG - FG) \\times 135");
    formulas.adjust(Formula::MoreySrm, "1.5\\,MCU^{0.7}");

    formulas.reset(Formula::AbvSimple);
    CHECK_FALSE(formulas.adjustment(Formula::AbvSimple));
    CHECK(calc::abv(1.050, 1.010) == near(0.0525, 1e-12));
    CHECK(formulas.adjustment(Formula::MoreySrm));

    formulas.reset_all();
    CHECK_FALSE(formulas.any_adjusted());
    CHECK(calc::srm_from_mcu(10.0, calc::ColorModel::Morey) == near(1.4922 * std::pow(10.0, 0.6859), 1e-12));
    app::FormulaService again(brewery.db, brewery.admin);
    CHECK_FALSE(again.any_adjusted());
}

TEST_CASE("saving the original formula puts it back") {
    Brewery brewery;
    app::FormulaService formulas(brewery.db, brewery.admin);
    formulas.adjust(Formula::AbvSimple, "(OG - FG) \\times 135");
    formulas.adjust(Formula::AbvSimple, calc::info(Formula::AbvSimple).latex);
    CHECK_FALSE(formulas.adjustment(Formula::AbvSimple));
}

TEST_CASE("a formula that can't be computed isn't saved") {
    Brewery brewery;
    app::FormulaService formulas(brewery.db, brewery.admin);
    CHECK_THROWS_AS(formulas.adjust(Formula::AbvSimple, "(OG - X) \\times 131.25"), calc::FormulaError);
    CHECK_THROWS_AS(formulas.adjust(Formula::MashPh, "5.4"), calc::FormulaError);
    CHECK_FALSE(formulas.any_adjusted());
    CHECK(calc::abv(1.050, 1.010) == near(0.0525, 1e-12));
}

TEST_CASE("a stored adjustment that no longer computes is skipped and reported") {
    Brewery brewery;
    brewery.db.exec(
        "INSERT INTO formula_adjustments (formula, latex, user_id, changed_at) "
        "VALUES ('abv_simple', '(OG - X)', 1, '2026-10-28T12:00:00Z');");
    app::FormulaService formulas(brewery.db, brewery.admin);
    CHECK_FALSE(formulas.adjustment(Formula::AbvSimple));
    REQUIRE(formulas.problems().size() == 1);
    CHECK(formulas.problems().front().starts_with("Alcohol by volume, simple: Ninkasi doesn't know"));
    CHECK(calc::abv(1.050, 1.010) == near(0.0525, 1e-12));
}

TEST_CASE("only admins adjust formulas") {
    Brewery brewery;
    app::UserService users(brewery.db, app::PasswordCost{1024, 1});
    users.set_password(brewery.admin, brewery.admin.user_id, "admin");
    const std::int64_t sam = users.add(brewery.admin, "Sam", domain::Role::Brewer, "");
    const app::Session brewer = *users.sign_in(sam, "");

    app::FormulaService formulas(brewery.db, brewer);
    CHECK_FALSE(formulas.can_adjust());
    CHECK_THROWS_AS(formulas.adjust(Formula::AbvSimple, "(OG - FG) \\times 135"), app::NotAllowed);
    CHECK_THROWS_AS(formulas.reset_all(), app::NotAllowed);
    CHECK_FALSE(formulas.any_adjusted());
}
