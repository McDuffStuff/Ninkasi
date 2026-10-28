#include "app/floor_service.h"
#include "app/picture.h"
#include "app/session.h"
#include "app/user_service.h"
#include "data/database.h"
#include "data/migrations.h"
#include "test_helpers.h"

#include <stdexcept>
#include <vector>

using namespace ninkasi;

namespace {

// A 2 × 1 PNG: a red pixel, then a half-transparent blue one.
const std::vector<unsigned char> kTinyPng = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xF4, 0x22, 0x7F, 0x8A, 0x00, 0x00, 0x00,
    0x0E, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0x00, 0x42, 0x0D, 0x00, 0x0F, 0x7A, 0x03,
    0x7E, 0x77, 0xE9, 0x7F, 0x97, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

struct Brewery {
    data::Database db{":memory:"};
    app::Session admin;

    Brewery() {
        data::migrate(db);
        admin = app::open_local_session(db);
    }
};

} // namespace

TEST_CASE("pictures are read into RGBA pixels") {
    const std::optional<app::PictureSize> size = app::picture_size(kTinyPng);
    REQUIRE(size);
    CHECK(size->width == 2);
    CHECK(size->height == 1);

    const std::optional<app::Pixels> pixels = app::decode_picture(kTinyPng);
    REQUIRE(pixels);
    CHECK(pixels->rgba == std::vector<unsigned char>{255, 0, 0, 255, 0, 0, 255, 128});

    const std::vector<unsigned char> not_a_picture = {'h', 'e', 'l', 'l', 'o'};
    CHECK_FALSE(app::picture_size(not_a_picture));
    CHECK_FALSE(app::decode_picture(not_a_picture));
    CHECK_FALSE(app::picture_size({}));
}

TEST_CASE("big pictures are halved until they fit, averaging the pixels") {
    app::Pixels pixels;
    pixels.width = 4;
    pixels.height = 2;
    pixels.rgba.assign(4 * 2 * 4, 0);
    for (std::size_t i = 0; i < pixels.rgba.size(); i += 8) pixels.rgba[i] = 200;  // every other pixel red
    const app::Pixels shrunk = app::shrink_to_fit(pixels, 2);
    CHECK(shrunk.width == 2);
    CHECK(shrunk.height == 1);
    CHECK(shrunk.rgba[0] == 100);
    CHECK(app::shrink_to_fit(pixels, 8).width == 4);  // already fits
}

TEST_CASE("the floor's background picture is kept with the brewery") {
    Brewery brewery;
    app::FloorService floor(brewery.db, brewery.admin);
    CHECK_FALSE(floor.background());
    const int revision = floor.background_revision();

    CHECK_THROWS_AS(floor.set_background({'n', 'o', 'p', 'e'}, "notes.txt"), std::invalid_argument);
    CHECK_FALSE(floor.background());

    floor.add(domain::ElementType::Fermenter, 100.0, 100.0);
    floor.set_background(kTinyPng, "plan.png");
    REQUIRE(floor.background());
    CHECK(floor.background()->pixel_width == 2);
    CHECK(floor.background()->file_name == "plan.png");
    CHECK(floor.background_revision() != revision);
    // Sized to cover the equipment: twice as wide as tall, like the picture.
    CHECK(floor.background()->height() == near(floor.background()->width / 2.0, 1e-9));

    floor.place_background(10.0, 20.0, 400.0, 2.0);
    CHECK(floor.background()->opacity == near(1.0, 1e-12));  // at most solid

    // Ninkasi starting again finds it where it was left.
    app::FloorService again(brewery.db, brewery.admin);
    REQUIRE(again.background());
    CHECK(again.background()->x == near(10.0, 1e-12));
    CHECK(again.background()->width == near(400.0, 1e-12));
    CHECK(again.background()->image == kTinyPng);

    again.remove_background();
    CHECK_FALSE(again.background());
    CHECK_FALSE(app::FloorService(brewery.db, brewery.admin).background());
}

TEST_CASE("only admins change the floor's background") {
    Brewery brewery;
    app::UserService users(brewery.db, app::PasswordCost{1024, 1});
    users.set_password(brewery.admin, brewery.admin.user_id, "admin");
    const app::Session brewer = *users.sign_in(users.add(brewery.admin, "Sam", domain::Role::Brewer, ""), "");
    app::FloorService floor(brewery.db, brewer);
    CHECK_THROWS_AS(floor.set_background(kTinyPng, "plan.png"), app::NotAllowed);
    CHECK_THROWS_AS(floor.remove_background(), app::NotAllowed);
}
