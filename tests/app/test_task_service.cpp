#include "app/floor_service.h"
#include "app/session.h"
#include "app/task_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <stdexcept>

using namespace ninkasi;
using domain::Date;
using domain::TaskUrgency;

namespace {

app::Session migrated_session(data::Database& db) {
    data::migrate(db);
    return app::open_local_session(db);
}

struct Brewery {
    data::Database db{":memory:"};
    app::Session session = migrated_session(db);
    app::FloorService floor{db, session};
    app::TaskService tasks{db, session};
    std::int64_t fermenter = floor.add(domain::ElementType::Fermenter, 0.0, 0.0);
};

domain::Task task(std::string title, std::optional<Date> due = std::nullopt,
                  std::optional<std::int64_t> element_id = std::nullopt) {
    domain::Task task;
    task.title = std::move(title);
    task.due = due;
    task.element_id = element_id;
    return task;
}

} // namespace

TEST_CASE("open tasks are listed by due date, undated last") {
    Brewery brewery;
    brewery.tasks.add(task("Order hops"));
    brewery.tasks.add(task("Dry hop", Date{2026, 11, 3}));
    brewery.tasks.add(task("Crash", Date{2026, 11, 1}));

    const std::vector<domain::Task>& open = brewery.tasks.open_tasks();
    REQUIRE(open.size() == 3);
    CHECK(open[0].title == "Crash");
    CHECK(open[1].title == "Dry hop");
    CHECK(open[2].title == "Order hops");
}

TEST_CASE("a task needs a title") {
    Brewery brewery;
    CHECK_THROWS_AS(brewery.tasks.add(task("   ")), std::invalid_argument);
    const std::int64_t id = brewery.tasks.add(task("  Clean lines  "));
    CHECK(brewery.tasks.open_tasks().front().title == "Clean lines");

    domain::Task blank = brewery.tasks.open_tasks().front();
    blank.title.clear();
    CHECK_THROWS_AS(brewery.tasks.update(blank), std::invalid_argument);
    CHECK(id > 0);
}

TEST_CASE("an element's urgency is its most pressing open task") {
    Brewery brewery;
    const Date today{2026, 11, 2};
    const std::int64_t fermenter = brewery.fermenter;
    CHECK(brewery.tasks.urgency_for(fermenter, today) == TaskUrgency::None);

    brewery.tasks.add(task("Dry hop", Date{2026, 11, 5}, fermenter));
    CHECK(brewery.tasks.urgency_for(fermenter, today) == TaskUrgency::Upcoming);
    const std::int64_t crash = brewery.tasks.add(task("Crash", Date{2026, 11, 2}, fermenter));
    CHECK(brewery.tasks.urgency_for(fermenter, today) == TaskUrgency::DueToday);
    brewery.tasks.add(task("Sample", Date{2026, 10, 30}, fermenter));
    CHECK(brewery.tasks.urgency_for(fermenter, today) == TaskUrgency::Overdue);
    CHECK(brewery.tasks.open_for(fermenter).size() == 3);
    CHECK(brewery.tasks.open_for(fermenter + 1).empty());

    // Done tasks don't count.
    brewery.tasks.complete(crash, domain::now());
    CHECK(brewery.tasks.open_for(fermenter).size() == 2);
}

TEST_CASE("completing a task logs it and can be undone") {
    Brewery brewery;
    const std::int64_t id = brewery.tasks.add(task("Dry hop", std::nullopt, brewery.fermenter));
    brewery.tasks.complete(id, domain::now());

    CHECK(brewery.tasks.open_tasks().empty());
    const std::vector<domain::Task> done = brewery.tasks.recently_done(brewery.fermenter);
    REQUIRE(done.size() == 1);
    CHECK(done.front().done());

    auto stmt = brewery.db.prepare("SELECT text FROM activity_log WHERE kind = 'task' AND element_id = ?;");
    stmt.bind(1, brewery.fermenter);
    REQUIRE(stmt.step());
    CHECK(stmt.column_text(0) == "Done: Dry hop");

    brewery.tasks.reopen(id);
    CHECK(brewery.tasks.open_tasks().size() == 1);
    CHECK(brewery.tasks.recently_done(std::nullopt).empty());
}

TEST_CASE("removing equipment removes its tasks") {
    Brewery brewery;
    brewery.tasks.add(task("Replace gasket", std::nullopt, brewery.fermenter));
    brewery.tasks.add(task("Order CO2"));
    brewery.floor.remove(brewery.fermenter);
    brewery.tasks.refresh();

    REQUIRE(brewery.tasks.open_tasks().size() == 1);
    CHECK(brewery.tasks.open_tasks().front().title == "Order CO2");
}
