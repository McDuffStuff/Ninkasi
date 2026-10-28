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
