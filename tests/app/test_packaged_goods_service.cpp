#include "app/packaged_goods_service.h"
#include "app/session.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <chrono>
#include <stdexcept>
#include <vector>

using namespace ninkasi;
using domain::KegCount;
using domain::KegState;
using domain::PackageType;

namespace {

app::Session migrated_session(data::Database& db) {
    data::migrate(db);
    return app::open_local_session(db);
}

domain::Timestamp day(int n) {
    return *domain::parse_iso8601("2026-10-01T08:00:00Z") + std::chrono::hours(24 * n);
}

struct KegRoom {
    data::Database db{":memory:"};
    app::Session session = migrated_session(db);
    app::PackagedGoodsService goods{db, session};

    int dirty(PackageType type) const { return goods.kegs(type).dirty; }
    int clean(PackageType type) const { return goods.kegs(type).clean; }
};

} // namespace

TEST_CASE("kegs arrive dirty and are cleaned") {
    KegRoom room;
    const std::vector<KegCount> added{{PackageType::HalfBarrel, 10}, {PackageType::Sixtel, 20}};
    room.goods.add_kegs(added, day(1), "new kegs");
    CHECK(room.dirty(PackageType::HalfBarrel) == 10);
    CHECK(room.dirty(PackageType::Sixtel) == 20);
    CHECK(room.clean(PackageType::Sixtel) == 0);

    const std::vector<KegCount> some{{PackageType::HalfBarrel, 4}};
    room.goods.clean_kegs(some, day(2));
    CHECK(room.dirty(PackageType::HalfBarrel) == 6);
    CHECK(room.clean(PackageType::HalfBarrel) == 4);

    room.goods.clean_all(day(3));
    CHECK(room.dirty(PackageType::HalfBarrel) == 0);
    CHECK(room.clean(PackageType::HalfBarrel) == 10);
    CHECK(room.dirty(PackageType::Sixtel) == 0);
    CHECK(room.clean(PackageType::Sixtel) == 20);
    CHECK_NOTHROW(room.goods.clean_all(day(4)));  // nothing left to clean

    const std::vector<data::KegMovementRow> history = room.goods.keg_history();
    REQUIRE(history.size() == 8);  // 2 added, then 3 cleanings of two rows each
    CHECK(history.back().movement.reason == domain::KegReason::Added);
    CHECK(history.back().movement.note == "new kegs");
}

TEST_CASE("more kegs than there are can't be cleaned or removed") {
    KegRoom room;
    const std::vector<KegCount> added{{PackageType::Sixtel, 5}};
    room.goods.add_kegs(added, day(1), "");

    const std::vector<KegCount> too_many{{PackageType::Sixtel, 3}, {PackageType::HalfBarrel, 1}};
    CHECK_THROWS_WITH_AS(room.goods.clean_kegs(too_many, day(2)), "There are no dirty half-barrels.",
                         std::invalid_argument);
    CHECK(room.dirty(PackageType::Sixtel) == 5);  // nothing was saved

    const std::vector<KegCount> lost{{PackageType::Sixtel, 2}};
    CHECK_THROWS_WITH_AS(room.goods.remove_kegs(lost, KegState::Clean, day(2), ""), "There are no clean sixtels.",
                         std::invalid_argument);
    room.goods.remove_kegs(lost, KegState::Dirty, day(2), "dented");
    CHECK(room.dirty(PackageType::Sixtel) == 3);

    const std::vector<KegCount> none{{PackageType::Sixtel, 0}};
    CHECK_THROWS_AS(room.goods.add_kegs(none, day(3), ""), std::invalid_argument);
    const std::vector<KegCount> cans{{PackageType::Can, 10}};
    CHECK_THROWS_AS(room.goods.add_kegs(cans, day(3), ""), std::invalid_argument);
}

namespace {

// Packaged beer to sell: two batches of Red Ale in half-barrels and one in
// cans, packaged on days 1, 2 and 3.
struct Cooler : KegRoom {
    double half = domain::keg_volume_l(PackageType::HalfBarrel);
    std::int64_t batch40 = batch("40");
    std::int64_t batch41 = batch("41");

    Cooler() {
        data::Transaction tx(db);
        goods.write_packaged(batch40, std::nullopt, PackageType::HalfBarrel, half, 10, day(1), "");
        goods.write_packaged(batch41, std::nullopt, PackageType::HalfBarrel, half, 6, day(2), "");
        goods.write_packaged(batch41, std::nullopt, PackageType::Can, 0.473, 96, day(3), "");
        tx.commit();
        goods.refresh();
    }

    std::int64_t batch(const std::string& code) {
        db.exec("INSERT INTO batches (code, name, recipe, status) VALUES ('" + code +
                "', 'Red Ale', '{}', 'packaged');");
        return db.last_insert_id();
    }

    int on_hand(std::int64_t batch_id, PackageType type) const {
        for (const domain::PackagedStock& entry : goods.packaged()) {
            if (entry.batch_id == batch_id && entry.type == type) return entry.count;
        }
        return 0;
    }
};

} // namespace

TEST_CASE("beer leaves packaged goods oldest batch first") {
    Cooler cooler;
    cooler.goods.remove({.beer = "red ale", .type = PackageType::HalfBarrel, .unit_volume_l = cooler.half,
                         .count = 12, .reason = domain::PackagedReason::Sold, .at = day(4)});
    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 0);
    CHECK(cooler.on_hand(cooler.batch41, PackageType::HalfBarrel) == 4);
    // Sold kegs leave with the beer.
    CHECK(cooler.goods.kegs(PackageType::HalfBarrel).dirty == 0);

    CHECK_THROWS_WITH_AS(cooler.goods.remove({.beer = "Red Ale", .type = PackageType::HalfBarrel,
                                              .unit_volume_l = cooler.half, .count = 5,
                                              .reason = domain::PackagedReason::Sold, .at = day(5)}),
                         "There are only 4 half-barrels of Red Ale.", std::invalid_argument);
    CHECK_THROWS_AS(cooler.goods.remove({.beer = "Red Ale", .type = PackageType::Can, .unit_volume_l = 0.355,
                                         .count = 1, .reason = domain::PackagedReason::Sold, .at = day(5)}),
                    std::invalid_argument);
}

TEST_CASE("kegs emptied at the brewery go back to the keg cleaner") {
    Cooler cooler;
    cooler.goods.remove({.beer = "Red Ale", .type = PackageType::HalfBarrel, .unit_volume_l = cooler.half,
                         .count = 2, .reason = domain::PackagedReason::Taproom, .at = day(4)});
    CHECK(cooler.goods.kegs(PackageType::HalfBarrel).dirty == 2);
    cooler.goods.remove({.beer = "Red Ale", .type = PackageType::Can, .unit_volume_l = 0.473, .count = 24,
                         .reason = domain::PackagedReason::Destroyed, .at = day(4)});
    CHECK(cooler.on_hand(cooler.batch41, PackageType::Can) == 72);
}

TEST_CASE("counts and returns adjust packaged goods") {
    Cooler cooler;
    cooler.goods.count("Red Ale", PackageType::HalfBarrel, cooler.half, 14, day(4), "two missing");
    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 8);  // oldest first
    cooler.goods.count("Red Ale", PackageType::HalfBarrel, cooler.half, 15, day(5), "");
    CHECK(cooler.on_hand(cooler.batch41, PackageType::HalfBarrel) == 7);  // newest gets the extra

    cooler.goods.return_packages(cooler.batch40, PackageType::HalfBarrel, cooler.half, 1, day(6), "flat");
    CHECK(cooler.on_hand(cooler.batch40, PackageType::HalfBarrel) == 9);
    CHECK_THROWS_AS(cooler.goods.return_packages(cooler.batch40, PackageType::Sixtel,
                                                 domain::keg_volume_l(PackageType::Sixtel), 1, day(6), ""),
                    std::invalid_argument);

    const std::vector<data::PackagedMovementRow> log = cooler.goods.packaged_history();
    REQUIRE_FALSE(log.empty());
    CHECK(log.front().movement.reason == domain::PackagedReason::Returned);
}
