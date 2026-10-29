#include "app/sales_service.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "data/settings_repository.h"
#include "test_helpers.h"

#include <stdexcept>

using namespace ninkasi;
using domain::Channel;

namespace {

struct Brewery {
    data::Database db{":memory:"};
    app::Session admin;
    data::SettingsRepository settings{db};

    Brewery() {
        data::migrate(db);
        admin = app::open_local_session(db);
    }

    app::Session brewer() {
        app::UserService users(db, app::PasswordCost{1024, 1});
        users.set_password(admin, admin.user_id, "admin");
        return *users.sign_in(users.add(admin, "Sam", domain::Role::Brewer, ""), "");
    }
};

domain::PriceEntry half_barrel(std::string beer, double price) {
    domain::PriceEntry entry;
    entry.beer = std::move(beer);
    entry.channel = Channel::Distributor;
    entry.package = domain::PackageType::HalfBarrel;
    entry.price = price;
    entry.effective_from = {2026, 1, 1};
    return entry;
}

} // namespace

TEST_CASE("customers are added and changed by anyone, and need a name") {
    Brewery brewery;
    app::SalesService sales(brewery.db, brewery.brewer(), brewery.settings);
    const std::int64_t id = sales.add_customer({0, "  Hopkins Distributing ", Channel::Distributor, "555-0100", "", true});
    REQUIRE(sales.find_customer(id));
    CHECK(sales.find_customer(id)->name == "Hopkins Distributing");
    CHECK_THROWS_AS(sales.add_customer({0, " ", Channel::Retail, "", "", true}), std::invalid_argument);

    domain::Customer gone = *sales.find_customer(id);
    gone.active = false;
    sales.update_customer(gone);
    app::SalesService again(brewery.db, brewery.admin, brewery.settings);
    REQUIRE(again.customers().size() == 1);
    CHECK_FALSE(again.customers().front().active);
}

TEST_CASE("the price list is kept, and only admins change it") {
    Brewery brewery;
    app::SalesService sales(brewery.db, brewery.admin, brewery.settings);
    sales.add_price(half_barrel("", 140.0));
    const std::int64_t hazy = sales.add_price(half_barrel("Hazy IPA", 160.0));

    domain::PriceQuery query;
    query.beer = "Red Ale";
    query.channel = Channel::Distributor;
    query.on = {2026, 3, 1};
    CHECK(sales.price_for(query)->price == 140.0);
    query.beer = "Hazy IPA";
    CHECK(sales.price_for(query)->price == 160.0);

    domain::PriceEntry changed = half_barrel("Hazy IPA", 165.0);
    changed.id = hazy;
    sales.update_price(changed);
    app::SalesService again(brewery.db, brewery.admin, brewery.settings);
    CHECK(again.price_for(query)->price == 165.0);
    again.remove_price(hazy);
    CHECK(again.price_for(query)->price == 140.0);

    CHECK_THROWS_AS(sales.add_price(half_barrel("", -1.0)), std::invalid_argument);
    domain::PriceEntry pour = half_barrel("", 7.0);
    pour.priced_as = domain::PricedAs::Pour;
    CHECK_THROWS_AS(sales.add_price(pour), std::invalid_argument);  // no pour size
    pour.pour_l = 0.473;
    pour.yield = 1.2;
    CHECK_THROWS_AS(sales.add_price(pour), std::invalid_argument);  // over 100 %
    pour.yield = 0.9;
    pour.channel = domain::Channel::Taproom;
    const std::int64_t pour_id = sales.add_price(pour);  // a pour, with no package, is stored
    bool found = false;
    for (const domain::PriceEntry& price : sales.prices()) {
        if (price.id != pour_id) continue;
        found = true;
        CHECK(price.priced_as == domain::PricedAs::Pour);
        CHECK(price.pour_l == doctest::Approx(0.473));
        CHECK(price.yield == doctest::Approx(0.9));
    }
    CHECK(found);

    app::SalesService brewer_sales(brewery.db, brewery.brewer(), brewery.settings);
    CHECK_FALSE(brewer_sales.can_set_prices());
    CHECK_THROWS_AS(brewer_sales.add_price(half_barrel("Red Ale", 1.0)), app::NotAllowed);
    CHECK_THROWS_AS(brewer_sales.set_currency({}), app::NotAllowed);
    CHECK(brewer_sales.price_for(query)->price == 140.0);  // everyone reads prices
}

TEST_CASE("the currency is kept with the brewery") {
    Brewery brewery;
    {
        app::SalesService sales(brewery.db, brewery.admin, brewery.settings);
        CHECK(sales.currency() == domain::Currency{});
        sales.set_currency({"€", true, true, 2});
    }
    app::SalesService again(brewery.db, brewery.admin, brewery.settings);
    CHECK(again.currency() == domain::Currency{"€", true, true, 2});
}
