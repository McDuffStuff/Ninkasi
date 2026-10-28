#include "domain/packaging.h"
#include "test_helpers.h"

#include <set>
#include <string>

using namespace ninkasi::domain;

TEST_CASE("kegs hold their standard volumes") {
    CHECK(keg_volume_l(PackageType::HalfBarrel) == near(58.674, 0.001));  // 15.5 US gallons
    CHECK(keg_volume_l(PackageType::Sixtel) == near(19.558, 0.001));      // 5.17 US gallons
    CHECK(keg_volume_l(PackageType::Can) == 0.0);
    CHECK(is_keg(PackageType::Sixtel));
    CHECK_FALSE(is_keg(PackageType::Can));
}

TEST_CASE("package counts read naturally") {
    CHECK(count_text(PackageType::HalfBarrel, 1) == "1 half-barrel");
    CHECK(count_text(PackageType::Sixtel, 20) == "20 sixtels");
    CHECK(count_text(PackageType::Can, 480) == "480 cans");
}

TEST_CASE("a fill takes clean kegs, then dirty ones, then ones the cleaner didn't know about") {
    CHECK(plan_keg_draw({.dirty = 8, .clean = 10}, 6) == KegDraw{.clean = 6});
    CHECK(plan_keg_draw({.dirty = 8, .clean = 6}, 10) == KegDraw{.clean = 6, .dirty = 4});
    CHECK(plan_keg_draw({.dirty = 2, .clean = 6}, 10) == KegDraw{.clean = 6, .dirty = 2, .unrecorded = 2});
    CHECK(plan_keg_draw({}, 3) == KegDraw{.unrecorded = 3});
    CHECK(plan_keg_draw({.dirty = 4, .clean = 4}, 0) == KegDraw{});
}

TEST_CASE("package and keg keys round-trip") {
    std::set<std::string> keys;
    for (PackageType type : kPackageTypes) {
        CHECK(keys.insert(std::string(key(type))).second);
        CHECK(package_type_from_key(key(type)) == type);
    }
    for (KegReason reason : {KegReason::Added, KegReason::Cleaned, KegReason::Filled, KegReason::Removed}) {
        CHECK(keg_reason_from_key(key(reason)) == reason);
    }
    CHECK(keg_state_from_key(key(KegState::Clean)) == KegState::Clean);
    CHECK_FALSE(package_type_from_key("growler").has_value());
}
