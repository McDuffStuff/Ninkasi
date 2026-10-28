#include "domain/element.h"

#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace ninkasi::domain;

TEST_CASE("element types have unique keys that round-trip") {
    std::set<std::string> keys;
    for (const ElementTypeInfo& type : element_types()) {
        const std::string key(type.key);
        CAPTURE(key);
        CHECK(keys.insert(key).second);
        CHECK(element_type_from_key(type.key) == type.type);
        CHECK(&info(type.type) == &type);
        CHECK_FALSE(type.name.empty());
        CHECK(type.default_width >= kMinElementSize);
        CHECK(type.default_height >= kMinElementSize);
    }
    CHECK_FALSE(element_type_from_key("spaceship").has_value());
}

TEST_CASE("vessels are the tanks that hold beer") {
    CHECK(info(ElementType::Fermenter).is_vessel);
    CHECK(info(ElementType::BriteTank).is_vessel);
    CHECK(info(ElementType::Brewhouse).is_vessel);
    CHECK_FALSE(info(ElementType::Inventory).is_vessel);
    CHECK_FALSE(info(ElementType::PackagedGoods).is_vessel);
}
