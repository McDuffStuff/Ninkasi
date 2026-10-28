#include "app/calc_settings.h"
#include "app/floor_service.h"
#include "app/recipe_service.h"
#include "app/session.h"
#include "app/ttb_service.h"
#include "app/unit_settings.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"

#include <doctest/doctest.h>

#include <optional>
#include <stdexcept>
#include <string>

using namespace ninkasi;
using domain::Role;

namespace {

// Cheap hashes keep the tests quick.
constexpr app::PasswordCost kQuick{1024, 1};

struct Brewery {
    data::Database db{":memory:"};
    app::UserService users{(data::migrate(db), db), kQuick};
    app::Session admin = users.only_person();

    std::optional<domain::User> person(std::int64_t id) {
        for (const domain::User& user : users.people()) {
            if (user.id == id) return user;
        }
        return std::nullopt;
    }

    // The admin sets a password, then adds a brewer (with `password`).
    std::int64_t add_brewer(const std::string& name, const std::string& password = "") {
        if (!person(admin.user_id)->has_password) users.set_password(admin, admin.user_id, "admin pass");
        return users.add(admin, name, Role::Brewer, password);
    }
};

} // namespace

TEST_CASE("a new brewery has one admin and opens without signing in") {
    Brewery brewery;
    CHECK_FALSE(brewery.users.sign_in_needed());
    CHECK(brewery.admin.is_admin());
    CHECK(brewery.admin.display_name == "Local Brewer");
    REQUIRE(brewery.users.people().size() == 1);
    CHECK(brewery.users.last_signed_in() == brewery.admin.user_id);
}

TEST_CASE("a password makes the admin sign in, with the right password only") {
    Brewery brewery;
    brewery.users.set_password(brewery.admin, brewery.admin.user_id, "kettle");
    CHECK(brewery.users.sign_in_needed());
    CHECK_THROWS_AS(brewery.users.only_person(), std::logic_error);

    CHECK_FALSE(brewery.users.sign_in(brewery.admin.user_id, "Kettle"));
    CHECK_FALSE(brewery.users.sign_in(brewery.admin.user_id, ""));
    const std::optional<app::Session> signed_in = brewery.users.sign_in(brewery.admin.user_id, "kettle");
    REQUIRE(signed_in);
    CHECK(signed_in->user_id == brewery.admin.user_id);
    CHECK(signed_in->is_admin());

    // Removing the only person's password goes back to opening straight in.
    brewery.users.set_password(brewery.admin, brewery.admin.user_id, "");
    CHECK_FALSE(brewery.users.sign_in_needed());
}

TEST_CASE("adding someone needs the admin to have a password first") {
    Brewery brewery;
    CHECK_THROWS_WITH_AS(brewery.users.add(brewery.admin, "Sam", Role::Brewer, ""),
                         doctest::Contains("Set a password for yourself first"), std::invalid_argument);
    CHECK(brewery.users.people().size() == 1);  // nothing was added

    const std::int64_t sam = brewery.add_brewer("Sam");
    CHECK(brewery.users.sign_in_needed());
    // A brewer without a password signs in by choosing their name.
    const std::optional<app::Session> session = brewery.users.sign_in(sam, "");
    REQUIRE(session);
    CHECK(session->display_name == "Sam");
    CHECK_FALSE(session->is_admin());
    CHECK(brewery.users.last_signed_in() == sam);
}

TEST_CASE("a second person makes sign-in needed even without passwords") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam");
    brewery.users.set_password(brewery.admin, sam, "");
    CHECK(brewery.users.sign_in_needed());
}

TEST_CASE("names are needed and unique, ignoring case") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam");
    CHECK_THROWS_AS(brewery.users.add(brewery.admin, "  ", Role::Brewer, ""), std::invalid_argument);
    CHECK_THROWS_WITH_AS(brewery.users.add(brewery.admin, "sam", Role::Brewer, ""),
                         "There's already someone called sam.", std::invalid_argument);
    CHECK_THROWS_AS(brewery.users.rename(brewery.admin, brewery.admin.user_id, "SAM"), std::invalid_argument);

    brewery.users.rename(brewery.admin, sam, "  Samantha ");
    CHECK(brewery.person(sam)->name == "Samantha");
    brewery.users.rename(brewery.admin, sam, "samantha");  // a change of case is fine
    CHECK(brewery.person(sam)->name == "samantha");
}

TEST_CASE("an admin always has a password while others use Ninkasi") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam");

    CHECK_THROWS_WITH_AS(brewery.users.add(brewery.admin, "Alex", Role::Admin, ""), "An admin needs a password.",
                         std::invalid_argument);
    CHECK_THROWS_WITH_AS(brewery.users.set_role(brewery.admin, sam, Role::Admin),
                         "Set a password for Sam before making them an admin.", std::invalid_argument);
    CHECK_THROWS_AS(brewery.users.set_password(brewery.admin, brewery.admin.user_id, ""), std::invalid_argument);
    CHECK_THROWS_AS(brewery.users.change_own_password(brewery.admin, "admin pass", ""), std::invalid_argument);

    brewery.users.set_password(brewery.admin, sam, "sam pass");
    brewery.users.set_role(brewery.admin, sam, Role::Admin);
    CHECK(brewery.person(sam)->role == Role::Admin);
}

TEST_CASE("there is always an active admin, and no one demotes or removes themselves") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam", "sam pass");

    CHECK_THROWS_AS(brewery.users.set_role(brewery.admin, brewery.admin.user_id, Role::Brewer),
                    std::invalid_argument);
    CHECK_THROWS_AS(brewery.users.set_active(brewery.admin, brewery.admin.user_id, false), std::invalid_argument);

    // With a second admin, the first can be made a brewer by them.
    brewery.users.set_role(brewery.admin, sam, Role::Admin);
    const app::Session sam_session = *brewery.users.sign_in(sam, "sam pass");
    brewery.users.set_role(sam_session, brewery.admin.user_id, Role::Brewer);
    CHECK(brewery.person(brewery.admin.user_id)->role == Role::Brewer);
    // Sam is now the only admin, and can't step down.
    CHECK_THROWS_AS(brewery.users.set_active(sam_session, sam, false), std::invalid_argument);
    CHECK_THROWS_AS(brewery.users.set_role(sam_session, sam, Role::Brewer), std::invalid_argument);
    CHECK(brewery.person(sam)->role == Role::Admin);
}

TEST_CASE("inactive people can't sign in, and keep their records") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam", "sam pass");
    brewery.users.set_active(brewery.admin, sam, false);

    CHECK_FALSE(brewery.users.sign_in(sam, "sam pass"));
    REQUIRE(brewery.person(sam));
    CHECK_FALSE(brewery.person(sam)->active);
    CHECK(brewery.users.people().back().id == sam);  // inactive people are listed last

    brewery.users.set_active(brewery.admin, sam, true);
    CHECK(brewery.users.sign_in(sam, "sam pass"));
}

TEST_CASE("people change their own password by giving the current one") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam", "old");
    const app::Session session = *brewery.users.sign_in(sam, "old");

    CHECK_THROWS_WITH_AS(brewery.users.change_own_password(session, "wrong", "new"),
                         "That isn't your current password.", std::invalid_argument);
    brewery.users.change_own_password(session, "old", "new");
    CHECK_FALSE(brewery.users.sign_in(sam, "old"));
    CHECK(brewery.users.sign_in(sam, "new"));

    // A brewer may go without one.
    brewery.users.change_own_password(session, "new", "");
    CHECK(brewery.users.sign_in(sam, "anything"));
    CHECK_FALSE(brewery.person(sam)->has_password);
}

TEST_CASE("a recovery code sets a new password at the sign-in screen, once") {
    Brewery brewery;
    const std::int64_t admin = brewery.admin.user_id;
    CHECK_THROWS_AS(brewery.users.make_recovery_code(brewery.admin), std::invalid_argument);  // no password yet
    brewery.users.set_password(brewery.admin, admin, "forgotten");
    const std::string code = brewery.users.make_recovery_code(brewery.admin);
    CHECK(brewery.person(admin)->has_recovery_code);

    // A wrong code changes nothing.
    CHECK_FALSE(brewery.users.recover(admin, "AAAA-AAAA-AAAA-AAAA-AAAA", "new pass"));
    CHECK(brewery.users.sign_in(admin, "forgotten"));
    CHECK_THROWS_AS(brewery.users.recover(admin, code, ""), std::invalid_argument);

    // Typed in lower case without dashes, it still works.
    std::string typed;
    for (const char c : code) {
        if (c != '-') typed += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    const std::optional<app::PasswordRecovery> recovered = brewery.users.recover(admin, typed, "new pass");
    REQUIRE(recovered);
    CHECK(recovered->session.user_id == admin);
    CHECK(recovered->session.is_admin());
    CHECK_FALSE(brewery.users.sign_in(admin, "forgotten"));
    CHECK(brewery.users.sign_in(admin, "new pass"));

    // The code is used up; the new one works instead.
    CHECK(recovered->new_code != code);
    CHECK_FALSE(brewery.users.recover(admin, code, "again"));
    CHECK(brewery.users.recover(admin, recovered->new_code, "again"));
}

TEST_CASE("a new recovery code replaces the old one, and removing the password removes it") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam", "sam pass");
    const app::Session session = *brewery.users.sign_in(sam, "sam pass");
    const std::string first = brewery.users.make_recovery_code(session);
    const std::string second = brewery.users.make_recovery_code(session);
    CHECK_FALSE(brewery.users.recover(sam, first, "new"));

    brewery.users.change_own_password(session, "sam pass", "");
    CHECK_FALSE(brewery.person(sam)->has_recovery_code);
    CHECK_FALSE(brewery.users.recover(sam, second, "new"));
}

TEST_CASE("inactive people can't recover a password") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam", "sam pass");
    const std::string code = brewery.users.make_recovery_code(*brewery.users.sign_in(sam, "sam pass"));
    brewery.users.set_active(brewery.admin, sam, false);
    CHECK_FALSE(brewery.users.recover(sam, code, "new"));
}

TEST_CASE("brewers can't manage people") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam");
    const app::Session session = *brewery.users.sign_in(sam, "");

    CHECK_THROWS_AS(brewery.users.add(session, "Alex", Role::Brewer, ""), app::NotAllowed);
    CHECK_THROWS_AS(brewery.users.rename(session, sam, "Samuel"), app::NotAllowed);
    CHECK_THROWS_AS(brewery.users.set_role(session, sam, Role::Admin), app::NotAllowed);
    CHECK_THROWS_AS(brewery.users.set_password(session, brewery.admin.user_id, "mine now"), app::NotAllowed);
    CHECK_THROWS_AS(brewery.users.set_active(session, brewery.admin.user_id, false), app::NotAllowed);
}

TEST_CASE("brewers can't set up the floor or change the brewery's settings") {
    Brewery brewery;
    app::FloorService admin_floor(brewery.db, brewery.admin);
    const std::int64_t tank = admin_floor.add(domain::ElementType::Fermenter, 0.0, 0.0);

    const std::int64_t sam = brewery.add_brewer("Sam");
    const app::Session brewer = *brewery.users.sign_in(sam, "");
    app::FloorService floor(brewery.db, brewer);
    CHECK_THROWS_AS(floor.add(domain::ElementType::Fermenter, 0.0, 0.0), app::NotAllowed);
    CHECK_THROWS_AS(floor.set_rect(tank, {0.0, 0.0, 100.0, 100.0}), app::NotAllowed);
    CHECK_THROWS_AS(floor.rename(tank, "FV9"), app::NotAllowed);
    CHECK_THROWS_AS(floor.set_capacity(tank, 1000.0), app::NotAllowed);
    CHECK_THROWS_AS(floor.remove(tank), app::NotAllowed);

    data::SettingsRepository settings(brewery.db);
    CHECK_THROWS_AS(app::save_unit_preferences(brewer, settings, calc::UnitPreferences::metric_brewery()),
                    app::NotAllowed);
    CHECK_THROWS_AS(app::save_calc_settings(brewer, settings, domain::CalcSettings{}), app::NotAllowed);
    app::RecipeService recipes(brewery.db, brewer);
    CHECK_THROWS_AS(recipes.set_defaults(recipes.defaults()), app::NotAllowed);
    app::TtbService ttb(brewery.db, brewer, settings);
    CHECK_THROWS_AS(ttb.set_cadence(domain::ReportCadence::Monthly), app::NotAllowed);
    CHECK_THROWS_WITH_AS(ttb.set_brewery_number("BR-1"), "Only an admin can change the TTB settings.",
                         app::NotAllowed);

    // Nothing changed.
    CHECK(app::load_unit_preferences(settings) == calc::UnitPreferences::us_brewery());
    CHECK(ttb.cadence() == domain::ReportCadence::Quarterly);
}

TEST_CASE("each person's settings are their own") {
    Brewery brewery;
    const std::int64_t sam = brewery.add_brewer("Sam");
    data::SettingsRepository brewery_wide(brewery.db);
    data::SettingsRepository mine(brewery.db, brewery.admin.user_id);
    data::SettingsRepository sams(brewery.db, sam);

    mine.set("ui.theme", "light");
    sams.set("ui.theme", "dark");
    brewery_wide.set("units.volume", "hectoliter");

    CHECK(mine.get("ui.theme") == "light");
    CHECK(sams.get("ui.theme") == "dark");
    CHECK_FALSE(brewery_wide.get("ui.theme"));
    CHECK_FALSE(mine.get("units.volume"));
    CHECK(brewery_wide.get("units.volume") == "hectoliter");
}
