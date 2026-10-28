#include "app/password.h"

#include <doctest/doctest.h>

#include <string>

using namespace ninkasi;

namespace {
// Cheap enough to keep the tests quick; the real cost is checked once below.
constexpr app::PasswordCost kQuick{1024, 1};
} // namespace

TEST_CASE("a password matches its own hash and nothing else") {
    const std::string hash = app::hash_password("mash tun", kQuick);
    CHECK(app::verify_password("mash tun", hash));
    CHECK_FALSE(app::verify_password("mash tun ", hash));
    CHECK_FALSE(app::verify_password("Mash tun", hash));
    CHECK_FALSE(app::verify_password("", hash));
}

TEST_CASE("the same password hashes differently each time") {
    const std::string first = app::hash_password("kettle", kQuick);
    const std::string second = app::hash_password("kettle", kQuick);
    CHECK(first != second);
    CHECK(app::verify_password("kettle", first));
    CHECK(app::verify_password("kettle", second));
}

TEST_CASE("passwords can hold any UTF-8 text") {
    const std::string hash = app::hash_password("Märzen 5°P ✓", kQuick);
    CHECK(app::verify_password("Märzen 5°P ✓", hash));
    CHECK_FALSE(app::verify_password("Marzen 5°P ✓", hash));
}

TEST_CASE("anything that isn't an Argon2id hash never matches") {
    CHECK_FALSE(app::verify_password("", ""));
    CHECK_FALSE(app::verify_password("secret", "secret"));
    CHECK_FALSE(app::verify_password("secret", "$argon2id$v=19$m=1024,t=1,p=1$garbage"));
}

TEST_CASE("recovery codes are 20 easily read letters and digits in groups of four") {
    const std::string code = app::new_recovery_code();
    REQUIRE(code.size() == 24);
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (i % 5 == 4) {
            CHECK(code[i] == '-');
        } else {
            CHECK(std::string_view("ABCDEFGHJKMNPQRSTUVWXYZ23456789").find(code[i]) != std::string_view::npos);
        }
    }
    CHECK(app::new_recovery_code() != code);
}

TEST_CASE("a recovery code is compared without its case, spaces or dashes") {
    CHECK(app::normalize_recovery_code("k7qf-3m9x TR2D") == "K7QF3M9XTR2D");
    CHECK(app::normalize_recovery_code(" - ") == "");
}

TEST_CASE("the default cost is OWASP's recommendation for Argon2id") {
    const std::string hash = app::hash_password("brewer");
    CHECK(hash.rfind("$argon2id$v=19$m=19456,t=2,p=1$", 0) == 0);
    CHECK(app::verify_password("brewer", hash));
}
