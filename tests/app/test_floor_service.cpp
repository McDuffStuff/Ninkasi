#include "app/floor_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <stdexcept>
#include <string>

using namespace ninkasi;
using domain::ElementType;

namespace {

struct Brewery {
    data::Database db{":memory:"};
    app::Session session;

    Brewery() {
        data::migrate(db);
        session = app::open_local_session(db);
    }

    std::int64_t count(const std::string& sql) {
        auto stmt = db.prepare(sql);
        stmt.step();
        return stmt.column_int64(0);
    }
};

} // namespace

TEST_CASE("new elements are named after their type and centered where placed") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.session);

    const auto first = floor.add(ElementType::Fermenter, 100.0, 200.0);
    const auto second = floor.add(ElementType::Fermenter, 300.0, 200.0);
    const auto brewhouse = floor.add(ElementType::Brewhouse, 0.0, 0.0);

    CHECK(floor.find(first)->name == "Fermenter 1");
    CHECK(floor.find(second)->name == "Fermenter 2");
    CHECK(floor.find(brewhouse)->name == "Brewhouse 1");

    const domain::ElementTypeInfo& type = domain::info(ElementType::Fermenter);
    const domain::Rect& rect = floor.find(first)->rect;
    CHECK(rect.x + rect.width / 2.0 == near(100.0, 1e-9));
    CHECK(rect.y + rect.height / 2.0 == near(200.0, 1e-9));
    CHECK(rect.width == type.default_width);
    CHECK(rect.height == type.default_height);
}

TEST_CASE("default names continue after the highest number in use") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.session);
    floor.add(ElementType::Fermenter, 0.0, 0.0);
    const auto second = floor.add(ElementType::Fermenter, 0.0, 0.0);
    const auto third = floor.add(ElementType::Fermenter, 0.0, 0.0);
    floor.remove(second);

    // Reusing "Fermenter 2" would make the next new tank a second "Fermenter 3".
    const auto fourth = floor.add(ElementType::Fermenter, 0.0, 0.0);
    CHECK(floor.find(fourth)->name == "Fermenter 4");

    // Custom names don't count.
    floor.rename(third, "Big Bertha");
    floor.rename(fourth, "Fermenter 4b");
    const auto next = floor.add(ElementType::Fermenter, 0.0, 0.0);
    CHECK(floor.find(next)->name == "Fermenter 2");
}

TEST_CASE("every change is saved and survives a reload") {
    Brewery brewery;
    std::int64_t id = 0;
    {
        app::FloorService floor(brewery.db, brewery.session);
        id = floor.add(ElementType::BriteTank, 0.0, 0.0);
        floor.set_rect(id, {10.0, 20.0, 150.0, 200.0});
        floor.rename(id, "  Brite 1  ");
        floor.set_capacity(id, 1173.48);
    }

    app::FloorService reloaded(brewery.db, brewery.session);
    REQUIRE(reloaded.elements().size() == 1);
    const domain::Element& element = reloaded.elements().front();
    CHECK(element.id == id);
    CHECK(element.type == ElementType::BriteTank);
    CHECK(element.name == "Brite 1");  // trimmed
    const domain::Rect expected_rect{10.0, 20.0, 150.0, 200.0};
    CHECK(element.rect == expected_rect);
    REQUIRE(element.capacity_l.has_value());
    CHECK(*element.capacity_l == near(1173.48, 1e-9));
}

TEST_CASE("elements can't shrink below the minimum size") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.session);
    const auto id = floor.add(ElementType::KegCleaner, 0.0, 0.0);
    floor.set_rect(id, {0.0, 0.0, 5.0, -10.0});
    CHECK(floor.find(id)->rect.width == domain::kMinElementSize);
    CHECK(floor.find(id)->rect.height == domain::kMinElementSize);
}

TEST_CASE("invalid changes are rejected without saving anything") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.session);
    const auto id = floor.add(ElementType::Fermenter, 0.0, 0.0);

    CHECK_THROWS_AS(floor.rename(id, "   "), std::invalid_argument);
    CHECK_THROWS_AS(floor.set_capacity(id, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(floor.rename(id + 100, "Ghost"), std::invalid_argument);
    CHECK(floor.find(id)->name == "Fermenter 1");
    CHECK_FALSE(floor.find(id)->capacity_l.has_value());
}

TEST_CASE("removing an element deletes it") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.session);
    const auto id = floor.add(ElementType::Fermenter, 0.0, 0.0);
    floor.remove(id);
    CHECK(floor.find(id) == nullptr);
    CHECK(brewery.count("SELECT COUNT(*) FROM elements;") == 0);
}

TEST_CASE("changes are audited under the user who made them") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.session);
    const auto id = floor.add(ElementType::Fermenter, 0.0, 0.0);
    floor.rename(id, "FV1");
    floor.set_capacity(id, 1000.0);
    floor.set_rect(id, {50.0, 50.0, 120.0, 160.0});  // layout only: not audited
    floor.remove(id);

    const std::string user = std::to_string(brewery.session.user_id);
    const std::string where = " AND user_id = " + user + " AND entity = 'element' AND entity_id = " + std::to_string(id);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action = 'element.create'" + where) == 1);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action = 'element.update'" + where) == 2);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log WHERE action = 'element.delete'" + where) == 1);
    CHECK(brewery.count("SELECT COUNT(*) FROM audit_log;") == 4);
}

TEST_CASE("rows this version doesn't understand are skipped when loading") {
    Brewery brewery;
    brewery.db.exec(
        "INSERT INTO elements (type, name, x, y, width, height) VALUES ('spaceship', 'USS Lager', 0, 0, 100, 100);"
        "INSERT INTO elements (type, name, x, y, width, height) VALUES ('fermenter', 'FV1', 0, 0, 100, 100);");

    app::FloorService floor(brewery.db, brewery.session);
    REQUIRE(floor.elements().size() == 1);
    CHECK(floor.elements().front().name == "FV1");
}
