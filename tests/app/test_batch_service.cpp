#include "app/batch_service.h"
#include "app/floor_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <chrono>
#include <stdexcept>
#include <string>

using namespace ninkasi;
using domain::BatchStatus;
using domain::ElementType;
using domain::VesselCondition;

namespace {

app::Session migrated_session(data::Database& db) {
    data::migrate(db);
    return app::open_local_session(db);
}

domain::Recipe pale_ale() {
    domain::Recipe recipe;
    recipe.name = "Pale Ale";
    recipe.style = "American Pale Ale";
    recipe.batch_volume_l = 1000.0;
    recipe.fermentables.push_back({0, {.name = "Pale malt"}, 200.0});
    recipe.hops.push_back({.hop = {.name = "Cascade"}, .mass_kg = 1.0});
    return recipe;
}

// Brew day plus `hours`.
domain::Timestamp at(int hours) {
    return *domain::parse_iso8601("2026-10-01T08:00:00Z") + std::chrono::hours(hours);
}

// A small brewery: one brewhouse, two 2000 L fermenters, a 500 L fermenter
// and a 2000 L brite tank.
struct Cellar {
    data::Database db{":memory:"};
    app::Session session = migrated_session(db);
    app::FloorService floor{db, session};
    std::int64_t brewhouse = floor.add(ElementType::Brewhouse, 0.0, 0.0);
    std::int64_t fv1 = add_vessel(ElementType::Fermenter, 2000.0);
    std::int64_t fv2 = add_vessel(ElementType::Fermenter, 2000.0);
    std::int64_t small = add_vessel(ElementType::Fermenter, 500.0);
    std::int64_t brite = add_vessel(ElementType::BriteTank, 2000.0);
    app::BatchService batches{db, session, floor};

    std::int64_t add_vessel(ElementType type, double capacity_l) {
        const std::int64_t id = floor.add(type, 0.0, 0.0);
        floor.set_capacity(id, capacity_l);
        return id;
    }

    std::int64_t start(const std::string& code = "") {
        return batches.start_brew({.brewhouse_id = brewhouse,
                                   .at = at(0),
                                   .code = code.empty() ? batches.suggest_code() : code,
                                   .recipe = pale_ale()});
    }

    app::KnockOut knock_out_request(std::int64_t brew_id, std::int64_t vessel, double volume_l) {
        return {.brew_id = brew_id,
                .vessel_id = vessel,
                .at = at(6),
                .volume_l = volume_l,
                .og = 1.050,
                .ingredients = domain::planned_ingredients(pale_ale())};
    }

    // Brews a new batch into `vessel` and returns the batch's id.
    std::int64_t brew(std::int64_t vessel, double volume_l, const std::string& code = "") {
        batches.knock_out(knock_out_request(start(code), vessel, volume_l));
        return batches.contents(vessel)->batch_id;
    }

    double held(std::int64_t vessel) const {
        const std::optional<app::VesselContents> contents = batches.contents(vessel);
        return contents ? contents->volume_l : 0.0;
    }

    double number(const std::string& sql) {
        auto stmt = db.prepare(sql);
        stmt.step();
        return stmt.column_double(0);
    }
};

} // namespace

TEST_CASE("brewing a batch fills a fermenter") {
    Cellar cellar;
    const std::int64_t brew_id = cellar.start("41");

    const domain::Brew* brewing = cellar.batches.brew_at(cellar.brewhouse);
    REQUIRE(brewing);
    CHECK(brewing->id == brew_id);
    REQUIRE(cellar.batches.active_batches().size() == 1);
    const std::int64_t batch_id = cellar.batches.active_batches().front().id;
    CHECK(cellar.batches.active_batches().front().status == BatchStatus::Brewing);
    CHECK_FALSE(cellar.batches.contents(cellar.fv1));

    cellar.batches.knock_out(cellar.knock_out_request(brew_id, cellar.fv1, 1000.0));

    CHECK_FALSE(cellar.batches.brew_at(cellar.brewhouse));
    REQUIRE(cellar.batches.contents(cellar.fv1));
    CHECK(cellar.batches.contents(cellar.fv1)->batch_id == batch_id);
    CHECK(cellar.held(cellar.fv1) == 1000.0);

    const domain::Batch* batch = cellar.batches.find_active(batch_id);
    REQUIRE(batch);
    CHECK(batch->status == BatchStatus::Fermenting);
    CHECK(batch->name() == "Pale Ale");
    CHECK(batch->code == "41");
    CHECK(*batch->og() == near(1.050, 1e-9));
    CHECK(batch->brewed_at() == at(6));

    // The confirmed ingredients and the ledger row are stored.
    const std::vector<domain::IngredientUse> used = cellar.batches.ingredients(brew_id);
    REQUIRE(used.size() == 2);
    CHECK(used[0].name == "Pale malt");
    CHECK(used[0].actual == 200.0);
    const std::vector<domain::VolumeEvent> ledger = cellar.batches.volume_history(batch_id);
    REQUIRE(ledger.size() == 1);
    CHECK(ledger[0].reason == domain::VolumeReason::Brewed);
    CHECK(ledger[0].vessel_id == cellar.fv1);
    CHECK(cellar.batches.batch_activity(batch_id).size() == 2);  // started, knocked out
    CHECK(cellar.batches.element_activity(cellar.brewhouse).size() == 2);
}

TEST_CASE("the batch keeps its own copy of the recipe") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    const std::optional<domain::Batch> stored = cellar.batches.load(batch_id);
    REQUIRE(stored);
    CHECK(stored->recipe.fermentables == pale_ale().fermentables);
    CHECK(stored->recipe.style == "American Pale Ale");
}

TEST_CASE("batch codes are suggested in sequence and must be unique") {
    Cellar cellar;
    CHECK(cellar.batches.suggest_code() == "1");
    cellar.brew(cellar.fv1, 1000.0, "26-009");
    CHECK(cellar.batches.suggest_code() == "26-010");
    CHECK_THROWS_AS(cellar.start("26-009"), std::invalid_argument);
    CHECK_THROWS_AS(cellar.start("  "), std::invalid_argument);
}

TEST_CASE("a brewhouse runs one brew at a time") {
    Cellar cellar;
    cellar.start();
    CHECK_THROWS_AS(cellar.start(), std::invalid_argument);
}

TEST_CASE("knock-out goes only into an empty beer vessel that is big enough") {
    Cellar cellar;
    cellar.brew(cellar.fv1, 1000.0);
    const std::int64_t brew_id = cellar.start();

    const app::Availability occupied = cellar.batches.can_knock_out(brew_id, cellar.fv1, 1000.0);
    CHECK_FALSE(occupied.allowed);
    CHECK(occupied.message == "Holds #1 Pale Ale.");
    CHECK_FALSE(cellar.batches.can_knock_out(brew_id, cellar.small, 1000.0).allowed);
    CHECK_FALSE(cellar.batches.can_knock_out(brew_id, cellar.brewhouse, 100.0).allowed);
    CHECK(cellar.batches.can_knock_out(brew_id, cellar.small, 500.0).allowed);

    CHECK_THROWS_AS(cellar.batches.knock_out(cellar.knock_out_request(brew_id, cellar.fv1, 1000.0)),
                    std::invalid_argument);
    CHECK_THROWS_AS(cellar.batches.knock_out(cellar.knock_out_request(brew_id, cellar.small, 1000.0)),
                    std::invalid_argument);
    CHECK(cellar.batches.brew_at(cellar.brewhouse));  // still brewing after the refusals

    cellar.batches.knock_out(cellar.knock_out_request(brew_id, cellar.fv2, 1000.0));
    CHECK(cellar.held(cellar.fv2) == 1000.0);
}

TEST_CASE("another brew can top up its own batch") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    const std::int64_t second =
        cellar.batches.start_brew({.brewhouse_id = cellar.brewhouse, .at = at(8), .add_to_batch = batch_id});

    CHECK_FALSE(cellar.batches.can_knock_out(second, cellar.fv1, 1200.0).allowed);  // only 1000 L free
    app::KnockOut request = cellar.knock_out_request(second, cellar.fv1, 500.0);
    request.og = 1.056;
    cellar.batches.knock_out(request);

    CHECK(cellar.held(cellar.fv1) == 1500.0);
    const domain::Batch* batch = cellar.batches.find_active(batch_id);
    REQUIRE(batch);
    CHECK(batch->brews.size() == 2);
    CHECK(*batch->og() == near(1.052, 1e-9));
    CHECK(cellar.batches.active_batches().size() == 1);
}

TEST_CASE("transfers are refused into occupied, undersized or out-of-service vessels") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    cellar.brew(cellar.fv2, 1000.0);

    CHECK_FALSE(cellar.batches.can_transfer(cellar.fv1, cellar.fv2, 1000.0).allowed);
    CHECK_FALSE(cellar.batches.can_transfer(cellar.fv1, cellar.small, 1000.0).allowed);
    CHECK_FALSE(cellar.batches.can_transfer(cellar.fv1, cellar.fv1, 1000.0).allowed);
    CHECK_FALSE(cellar.batches.can_transfer(cellar.small, cellar.brite, 100.0).allowed);  // nothing to move

    cellar.batches.set_condition(cellar.brite, VesselCondition::OutOfService, at(10));
    CHECK_FALSE(cellar.batches.can_transfer(cellar.fv1, cellar.brite, 1000.0).allowed);
    CHECK_THROWS_AS(cellar.batches.transfer({.from_vessel_id = cellar.fv1, .to_vessel_id = cellar.brite,
                                             .at = at(11), .volume_l = 970.0}),
                    std::invalid_argument);
    CHECK(cellar.held(cellar.fv1) == 1000.0);

    cellar.batches.set_condition(cellar.brite, VesselCondition::Clean, at(12));
    cellar.batches.transfer({.from_vessel_id = cellar.fv1, .to_vessel_id = cellar.brite, .at = at(13),
                             .volume_l = 970.0});

    CHECK_FALSE(cellar.batches.contents(cellar.fv1));
    CHECK(cellar.held(cellar.brite) == 970.0);
    CHECK(cellar.batches.condition(cellar.fv1) == VesselCondition::Dirty);
    // The 30 L left behind is a loss, and the brite tank starts conditioning.
    CHECK(cellar.number("SELECT SUM(volume_l) FROM volume_events WHERE reason = 'loss'") == -30.0);
    CHECK(cellar.batches.find_active(batch_id)->status == BatchStatus::Conditioning);

    // A dirty vessel can still be filled, with a warning.
    const app::Availability dirty = cellar.batches.can_transfer(cellar.brite, cellar.fv1, 970.0);
    CHECK(dirty.allowed);
    CHECK(dirty.message == "Needs cleaning.");
}

TEST_CASE("splitting a batch leaves the rest in the source vessel") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    cellar.batches.transfer({.from_vessel_id = cellar.fv1, .to_vessel_id = cellar.brite, .at = at(20),
                             .volume_l = 400.0, .empties_source = false});

    CHECK(cellar.held(cellar.fv1) == 600.0);
    CHECK(cellar.held(cellar.brite) == 400.0);
    CHECK(cellar.batches.locations(batch_id).size() == 2);
    CHECK(cellar.batches.volume(batch_id) == 1000.0);
    CHECK(cellar.batches.condition(cellar.fv1) == VesselCondition::Clean);

    // Moving all of it is a full transfer, not a split.
    CHECK_THROWS_AS(cellar.batches.transfer({.from_vessel_id = cellar.fv1, .to_vessel_id = cellar.fv2, .at = at(21),
                                             .volume_l = 600.0, .empties_source = false}),
                    std::invalid_argument);
}

TEST_CASE("packaging the last of a batch finishes it") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);

    cellar.batches.package({.vessel_id = cellar.fv1, .at = at(100), .volume_l = 400.0, .empties_vessel = false});
    CHECK(cellar.held(cellar.fv1) == 600.0);
    CHECK(cellar.batches.find_active(batch_id));

    cellar.batches.package({.vessel_id = cellar.fv1, .format = domain::PackageFormat::CansOrBottles, .at = at(101),
                            .volume_l = 590.0});
    CHECK_FALSE(cellar.batches.contents(cellar.fv1));
    CHECK_FALSE(cellar.batches.find_active(batch_id));
    CHECK(cellar.batches.condition(cellar.fv1) == VesselCondition::Dirty);

    const std::optional<domain::Batch> finished = cellar.batches.load(batch_id);
    REQUIRE(finished);
    CHECK(finished->status == BatchStatus::Packaged);
    CHECK(finished->finished_at == at(101));
    CHECK(cellar.number("SELECT SUM(volume_l) FROM volume_events WHERE reason = 'kegged'") == -400.0);
    CHECK(cellar.number("SELECT SUM(volume_l) FROM volume_events WHERE reason = 'bottled'") == -590.0);
    CHECK(cellar.number("SELECT SUM(volume_l) FROM volume_events WHERE reason = 'loss'") == -10.0);
}

TEST_CASE("dumping a batch finishes it as dumped") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    cellar.batches.dump(cellar.fv1, at(50), "infected");

    CHECK_FALSE(cellar.batches.contents(cellar.fv1));
    CHECK(cellar.batches.load(batch_id)->status == BatchStatus::Dumped);
    const std::vector<domain::Activity> log = cellar.batches.batch_activity(batch_id);
    REQUIRE_FALSE(log.empty());
    CHECK(log.front().kind == "finished");
    CHECK(log[1].text.ends_with("from Fermenter 1: infected"));
}

TEST_CASE("correcting a volume records the difference") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    cellar.batches.correct_volume(cellar.fv1, 1015.0, at(30), "dipped the tank");

    CHECK(cellar.held(cellar.fv1) == 1015.0);
    const std::vector<domain::VolumeEvent> ledger = cellar.batches.volume_history(batch_id);
    REQUIRE(ledger.size() == 2);
    CHECK(ledger[1].reason == domain::VolumeReason::Adjusted);
    CHECK(ledger[1].volume_l == near(15.0, 1e-9));
}

TEST_CASE("cancelling a new batch's only brew forgets the batch") {
    Cellar cellar;
    const std::int64_t brew_id = cellar.start("7");
    cellar.batches.cancel_brew(brew_id);

    CHECK(cellar.batches.active_batches().empty());
    CHECK_FALSE(cellar.batches.brew_at(cellar.brewhouse));
    CHECK(cellar.batches.suggest_code() == "1");  // the code is free again
    CHECK(cellar.number("SELECT COUNT(*) FROM audit_log WHERE action = 'batch.cancel'") == 1.0);

    // Cancelling another brew of an existing batch keeps the batch.
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);
    const std::int64_t second =
        cellar.batches.start_brew({.brewhouse_id = cellar.brewhouse, .at = at(8), .add_to_batch = batch_id});
    cellar.batches.cancel_brew(second);
    REQUIRE(cellar.batches.find_active(batch_id));
    CHECK(cellar.batches.find_active(batch_id)->brews.size() == 1);
}

TEST_CASE("knock-out is all or nothing") {
    Cellar cellar;
    const std::int64_t brew_id = cellar.start();
    // Make the ledger write fail after the brew and ingredients are written.
    cellar.db.exec("CREATE TRIGGER fail_ledger BEFORE INSERT ON volume_events BEGIN SELECT RAISE(ABORT, 'disk full'); END;");

    CHECK_THROWS_AS(cellar.batches.knock_out(cellar.knock_out_request(brew_id, cellar.fv1, 1000.0)), data::DbError);

    CHECK(cellar.batches.brew_at(cellar.brewhouse));
    CHECK(cellar.batches.active_batches().front().status == BatchStatus::Brewing);
    CHECK(cellar.number("SELECT COUNT(*) FROM brew_ingredients") == 0.0);
    CHECK(cellar.number("SELECT COUNT(*) FROM brews WHERE knocked_out_at IS NOT NULL") == 0.0);
    CHECK(cellar.number("SELECT COUNT(*) FROM activity_log WHERE kind = 'knock_out'") == 0.0);
}

TEST_CASE("the ledger reconciles with what the vessels hold") {
    Cellar cellar;
    const std::int64_t a = cellar.brew(cellar.fv1, 1000.0);
    const std::int64_t b = cellar.brew(cellar.fv2, 1200.0);
    cellar.batches.transfer({.from_vessel_id = cellar.fv1, .to_vessel_id = cellar.brite, .at = at(40),
                             .volume_l = 980.0});
    cellar.batches.transfer({.from_vessel_id = cellar.fv2, .to_vessel_id = cellar.small, .at = at(41),
                             .volume_l = 300.0, .empties_source = false});
    cellar.batches.correct_volume(cellar.fv2, 905.0, at(42), "");
    cellar.batches.package({.vessel_id = cellar.brite, .at = at(43), .volume_l = 500.0, .empties_vessel = false});
    // More came out than was recorded: the shortfall is booked as a gain first.
    cellar.batches.transfer({.from_vessel_id = cellar.small, .to_vessel_id = cellar.fv1, .at = at(44),
                             .volume_l = 310.0});

    // Each vessel's ledger rows add up to what it holds.
    for (std::int64_t vessel : {cellar.fv1, cellar.fv2, cellar.small, cellar.brite}) {
        const double ledger = cellar.number("SELECT COALESCE(SUM(volume_l), 0) FROM volume_events WHERE vessel_id = " +
                                            std::to_string(vessel));
        CHECK(ledger == near(cellar.held(vessel), 1e-6));
    }
    // Each batch's volume is what was brewed, less what left, plus corrections.
    CHECK(cellar.batches.volume(a) == near(1000.0 - 20.0 - 500.0, 1e-6));
    CHECK(cellar.batches.volume(b) == near(1200.0 + 5.0 + 10.0, 1e-6));
    // Transfers only move beer.
    CHECK(cellar.number("SELECT SUM(volume_l) FROM volume_events WHERE reason IN ('transfer_in', 'transfer_out')") ==
          near(0.0, 1e-9));
    CHECK(cellar.held(cellar.fv1) == near(310.0, 1e-6));
    CHECK_FALSE(cellar.batches.contents(cellar.small));
}

TEST_CASE("equipment in use can't be removed from the floor") {
    Cellar cellar;
    cellar.start();
    CHECK(cellar.floor.removal_blocker(cellar.brewhouse));
    CHECK_THROWS_AS(cellar.floor.remove(cellar.brewhouse), std::invalid_argument);

    cellar.batches.knock_out(cellar.knock_out_request(cellar.batches.brew_at(cellar.brewhouse)->id, cellar.fv1, 900.0));
    CHECK_FALSE(cellar.floor.removal_blocker(cellar.brewhouse));
    CHECK_THROWS_AS(cellar.floor.remove(cellar.fv1), std::invalid_argument);

    cellar.batches.dump(cellar.fv1, at(9), "");
    CHECK_NOTHROW(cellar.floor.remove(cellar.fv1));
}

TEST_CASE("readings are checked, kept in order and can be deleted") {
    Cellar cellar;
    const std::int64_t batch_id = cellar.brew(cellar.fv1, 1000.0);

    CHECK_THROWS_AS(cellar.batches.log_reading({.batch_id = batch_id, .at = at(24)}), std::invalid_argument);
    CHECK_THROWS_AS(cellar.batches.log_reading({.batch_id = batch_id, .at = at(24), .gravity_sg = 10.12}),
                    std::invalid_argument);
    CHECK_THROWS_AS(cellar.batches.log_reading({.batch_id = batch_id, .at = at(24), .ph = 15.0}),
                    std::invalid_argument);

    cellar.batches.log_reading({.batch_id = batch_id, .vessel_id = cellar.fv1, .at = at(72), .gravity_sg = 1.020});
    const std::int64_t latest = cellar.batches.log_reading(
        {.batch_id = batch_id, .vessel_id = cellar.fv1, .at = at(120), .gravity_sg = 1.011, .temperature_c = 19.5});
    cellar.batches.log_reading({.batch_id = batch_id, .at = at(48), .gravity_sg = 1.035});  // logged late

    const domain::Batch* batch = cellar.batches.find_active(batch_id);
    REQUIRE(batch);
    REQUIRE(batch->readings.size() == 3);
    CHECK(batch->readings.front().gravity_sg == 1.035);  // sorted by when they were taken
    CHECK(*batch->latest_gravity()->gravity_sg == 1.011);

    cellar.batches.delete_reading(latest);
    CHECK(*cellar.batches.find_active(batch_id)->latest_gravity()->gravity_sg == 1.020);
}

TEST_CASE("vessel conditions apply to empty fermenters and brite tanks") {
    Cellar cellar;
    CHECK(cellar.batches.condition(cellar.fv1) == VesselCondition::Clean);
    cellar.batches.set_condition(cellar.fv1, VesselCondition::Dirty, at(1));
    CHECK(cellar.batches.condition(cellar.fv1) == VesselCondition::Dirty);
    CHECK_THROWS_AS(cellar.batches.set_condition(cellar.brewhouse, VesselCondition::Dirty, at(1)),
                    std::invalid_argument);

    // Filling a vessel clears its condition; it can't be changed while full.
    cellar.brew(cellar.fv1, 1000.0);
    CHECK(cellar.batches.condition(cellar.fv1) == VesselCondition::Clean);
    CHECK_THROWS_AS(cellar.batches.set_condition(cellar.fv1, VesselCondition::OutOfService, at(2)),
                    std::invalid_argument);
}
