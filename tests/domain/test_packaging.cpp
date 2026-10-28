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

TEST_CASE("packaged beer combines by beer and package across batches") {
    const Timestamp day1 = *parse_iso8601("2026-10-01T08:00:00Z");
    const Timestamp day2 = *parse_iso8601("2026-10-02T08:00:00Z");
    const PackagedStock stock[] = {
        {.batch_id = 2, .batch_code = "42", .beer = "Hazy IPA", .type = PackageType::Sixtel,
         .unit_volume_l = keg_volume_l(PackageType::Sixtel), .count = 4, .packaged_at = day2},
        {.batch_id = 1, .batch_code = "41", .beer = "Hazy IPA", .type = PackageType::Sixtel,
         .unit_volume_l = keg_volume_l(PackageType::Sixtel), .count = 6, .packaged_at = day1},
        {.batch_id = 1, .batch_code = "41", .beer = "Hazy IPA", .type = PackageType::HalfBarrel,
         .unit_volume_l = keg_volume_l(PackageType::HalfBarrel), .count = 2, .packaged_at = day1},
        {.batch_id = 3, .batch_code = "43", .beer = "hazy ipa", .type = PackageType::Can, .unit_volume_l = 0.473,
         .count = 96, .packaged_at = day2},
        {.batch_id = 4, .batch_code = "44", .beer = "Hazy IPA", .type = PackageType::Can, .unit_volume_l = 0.355,
         .count = 48, .packaged_at = day2},
        {.batch_id = 5, .batch_code = "45", .beer = "Amber", .type = PackageType::Sixtel,
         .unit_volume_l = keg_volume_l(PackageType::Sixtel), .count = 3, .packaged_at = day1},
    };
    const std::vector<PackagedProduct> products = combine_by_beer(stock);
    REQUIRE(products.size() == 5);
    CHECK(products[0].beer == "Amber");
    // Hazy IPA: half-barrels, sixtels, then each size of can.
    CHECK(products[1].type == PackageType::HalfBarrel);
    CHECK(products[2].type == PackageType::Sixtel);
    CHECK(products[2].count == 10);
    REQUIRE(products[2].batches.size() == 2);
    CHECK(products[2].batches[0].batch_code == "41");  // oldest first
    CHECK(products[2].volume_l() == near(10 * keg_volume_l(PackageType::Sixtel), 1e-9));
    CHECK(products[3].unit_volume_l == 0.355);
    CHECK(products[4].count == 96);  // the name's case doesn't matter
}

TEST_CASE("package and keg keys round-trip") {
    std::set<std::string> keys;
    for (PackageType type : kPackageTypes) {
        CHECK(keys.insert(std::string(key(type))).second);
        CHECK(package_type_from_key(key(type)) == type);
    }
    for (KegReason reason :
         {KegReason::Added, KegReason::Cleaned, KegReason::Filled, KegReason::Removed, KegReason::Emptied}) {
        CHECK(keg_reason_from_key(key(reason)) == reason);
    }
    std::set<std::string> reasons;
    for (int i = 0; i <= static_cast<int>(PackagedReason::Returned); ++i) {
        const auto reason = static_cast<PackagedReason>(i);
        CHECK(reasons.insert(std::string(key(reason))).second);
        CHECK(packaged_reason_from_key(key(reason)) == reason);
    }
    CHECK(keg_state_from_key(key(KegState::Clean)) == KegState::Clean);
    CHECK_FALSE(package_type_from_key("growler").has_value());
}
