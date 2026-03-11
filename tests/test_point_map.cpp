#include <doctest/doctest.h>

#include "dnp3/PointMap.hpp"

using namespace dnp3bridge::dnp3;

TEST_CASE("PointMap") {

    PointMap map;

    SUBCASE("resolve unknown name returns nullopt") {
        auto result = map.resolve("nonexistent");
        CHECK_FALSE(result.has_value());
    }

    SUBCASE("add and resolve a single point") {
        map.add("temperature", {PointType::Analog, 0});

        auto result = map.resolve("temperature");
        REQUIRE(result.has_value());
        CHECK(result->type  == PointType::Analog);
        CHECK(result->index == 0);
    }

    SUBCASE("add multiple points of different types") {
        map.add("temperature", {PointType::Analog,  0});
        map.add("valve_open",  {PointType::Binary,  1});
        map.add("pulse_count", {PointType::Counter, 2});

        auto a = map.resolve("temperature");
        REQUIRE(a.has_value());
        CHECK(a->type  == PointType::Analog);
        CHECK(a->index == 0);

        auto b = map.resolve("valve_open");
        REQUIRE(b.has_value());
        CHECK(b->type  == PointType::Binary);
        CHECK(b->index == 1);

        auto c = map.resolve("pulse_count");
        REQUIRE(c.has_value());
        CHECK(c->type  == PointType::Counter);
        CHECK(c->index == 2);
    }

    SUBCASE("resolve with string_view works") {
        map.add("sensor_a", {PointType::Analog, 5});

        std::string_view name = "sensor_a";
        auto result = map.resolve(name);
        REQUIRE(result.has_value());
        CHECK(result->index == 5);
    }

    SUBCASE("add with same name uses first insertion (emplace semantics)") {
        // PointMap::add uses emplace, which does NOT overwrite.
        map.add("point", {PointType::Analog, 10});
        map.add("point", {PointType::Binary, 20});

        auto result = map.resolve("point");
        REQUIRE(result.has_value());
        // emplace keeps the first insertion
        CHECK(result->type  == PointType::Analog);
        CHECK(result->index == 10);
    }

    SUBCASE("empty name is a valid key") {
        map.add("", {PointType::Counter, 99});
        auto result = map.resolve("");
        REQUIRE(result.has_value());
        CHECK(result->index == 99);
    }
}
