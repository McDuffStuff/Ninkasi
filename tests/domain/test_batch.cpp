#include "domain/batch.h"
#include "test_helpers.h"

using namespace ninkasi::domain;

TEST_CASE("batch codes continue the brewery's numbering") {
    CHECK(next_batch_code("") == "1");
    CHECK(next_batch_code("1") == "2");
    CHECK(next_batch_code("41") == "42");
    CHECK(next_batch_code("99") == "100");
    CHECK(next_batch_code("26-009") == "26-010");  // zero padding kept
    CHECK(next_batch_code("B7") == "B8");
    CHECK(next_batch_code("2026-099") == "2026-100");
    CHECK(next_batch_code("IPA") == "IPA-2");       // no number to continue
}

TEST_CASE("batch labels read as a number and a beer") {
    CHECK(batch_label("142", "Hazy IPA") == "#142 Hazy IPA");
    CHECK(batch_label("#142", "Hazy IPA") == "#142 Hazy IPA");
    CHECK(batch_label("142", "") == "#142");
}

TEST_CASE("planned ingredients list each addition in its own unit") {
    Recipe recipe;
    recipe.fermentables.push_back({7, {.name = "Pale malt"}, 200.0});
    HopAddition boil{.library_id = 9, .hop = {.name = "Citra"}, .mass_kg = 1.5};
    boil.time_min = 60.0;
    HopAddition dry{.hop = {.name = "Citra"}, .mass_kg = 4.0, .use = ninkasi::calc::HopUse::DryHop};
    recipe.hops = {boil, dry};
    YeastAddition liquid{.yeast = {.name = "London Ale"}, .packs = 3.0};
    YeastAddition dried{.yeast = {.name = "US-05", .form = YeastForm::Dry}, .mass_kg = 0.5};
    recipe.yeasts = {liquid, dried};
    recipe.miscs.push_back({"Whirlfloc", "1 tablet", MiscUse::Boil, 15.0});

    const std::vector<IngredientUse> uses = planned_ingredients(recipe);
    REQUIRE(uses.size() == 6);
    CHECK(uses[0].kind == IngredientKind::Fermentable);
    CHECK(uses[0].library_id == 7);
    CHECK(uses[0].planned == 200.0);
    CHECK(uses[0].actual == 200.0);
    CHECK(uses[1].detail == "Boil, 60 min");
    CHECK(uses[1].library_id == 9);
    CHECK(uses[2].detail == "Dry hop");
    CHECK(uses[3].amount_kind == AmountKind::Count);
    CHECK(uses[3].planned == 3.0);
    CHECK(uses[4].amount_kind == AmountKind::Mass);
    CHECK(uses[4].planned == 0.5);
    CHECK(uses[5].amount_kind == AmountKind::Text);
    CHECK(uses[5].text == "1 tablet");
}

TEST_CASE("a batch's original gravity is its brews' averaged by volume") {
    Batch batch;
    CHECK_FALSE(batch.og());

    Brew first;
    first.knocked_out_at = Timestamp{std::chrono::seconds(1000)};
    first.volume_l = 1000.0;
    first.og = 1.060;
    Brew second = first;
    second.knocked_out_at = Timestamp{std::chrono::seconds(500)};
    second.volume_l = 500.0;
    second.og = 1.054;
    Brew brewing;  // still under way: not counted
    brewing.volume_l = 9999.0;
    brewing.og = 1.100;
    batch.brews = {first, second, brewing};

    CHECK(*batch.og() == near(1.058, 1e-9));
    CHECK(batch.brewed_at() == Timestamp{std::chrono::seconds(500)});
    CHECK(batch.brew_in_progress() == &batch.brews[2]);
}

TEST_CASE("the latest reading wins even if it was logged out of order") {
    Batch batch;
    Reading early{.at = Timestamp{std::chrono::seconds(100)}, .gravity_sg = 1.030, .temperature_c = 19.0};
    Reading late{.at = Timestamp{std::chrono::seconds(300)}, .gravity_sg = 1.012};
    Reading middle{.at = Timestamp{std::chrono::seconds(200)}, .temperature_c = 18.0};
    batch.readings = {late, early, middle};

    REQUIRE(batch.latest_gravity());
    CHECK(*batch.latest_gravity()->gravity_sg == 1.012);
    REQUIRE(batch.latest_temperature());
    CHECK(*batch.latest_temperature()->temperature_c == 18.0);
}

TEST_CASE("stored keys round-trip") {
    for (BatchStatus status : {BatchStatus::Brewing, BatchStatus::Fermenting, BatchStatus::Conditioning,
                               BatchStatus::Packaged, BatchStatus::Dumped}) {
        CHECK(batch_status_from_key(key(status)) == status);
    }
    for (VolumeReason reason : {VolumeReason::Brewed, VolumeReason::TransferIn, VolumeReason::TransferOut,
                                VolumeReason::Kegged, VolumeReason::Bottled, VolumeReason::Loss, VolumeReason::Dumped,
                                VolumeReason::Adjusted}) {
        CHECK(volume_reason_from_key(key(reason)) == reason);
    }
    for (ReadingMethod method : kReadingMethods) CHECK(reading_method_from_key(key(method)) == method);
    CHECK_FALSE(batch_status_from_key("bottled"));
}
