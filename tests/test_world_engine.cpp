#include <catch2/catch_test_macros.hpp>
#include <clove/world_engine.hpp>

using namespace clove;

TEST_CASE("WorldEngine create returns non-zero id", "[world]") {
    WorldEngine engine;
    uint32_t id = engine.create("test-world");
    REQUIRE(id != 0);
}

TEST_CASE("WorldEngine create multiple worlds — unique ids", "[world]") {
    WorldEngine engine;
    uint32_t id1 = engine.create("world-a");
    uint32_t id2 = engine.create("world-b");
    uint32_t id3 = engine.create("world-c");

    REQUIRE(id1 != id2);
    REQUIRE(id2 != id3);
    REQUIRE(id1 != id3);
}

TEST_CASE("WorldEngine destroy existing vs non-existing", "[world]") {
    WorldEngine engine;
    uint32_t id = engine.create("ephemeral");

    REQUIRE(engine.destroy(id));
    REQUIRE_FALSE(engine.destroy(id));
    REQUIRE_FALSE(engine.destroy(99999));
}

TEST_CASE("WorldEngine list shows created worlds", "[world]") {
    WorldEngine engine;
    engine.create("alpha");
    engine.create("beta");

    auto worlds = engine.list();
    REQUIRE(worlds.size() == 2);

    bool found_alpha = false, found_beta = false;
    for (const auto& w : worlds) {
        if (w.name == "alpha") found_alpha = true;
        if (w.name == "beta") found_beta = true;
        REQUIRE(w.member_count == 0);
    }
    REQUIRE(found_alpha);
    REQUIRE(found_beta);
}

TEST_CASE("WorldEngine join and leave change member_count", "[world]") {
    WorldEngine engine;
    uint32_t wid = engine.create("social");

    REQUIRE(engine.join(wid, 1));
    REQUIRE(engine.join(wid, 2));

    auto worlds = engine.list();
    REQUIRE(worlds.size() == 1);
    REQUIRE(worlds[0].member_count == 2);

    REQUIRE(engine.leave(wid, 1));
    worlds = engine.list();
    REQUIRE(worlds[0].member_count == 1);
}

TEST_CASE("WorldEngine set_state then get_state roundtrip", "[world]") {
    WorldEngine engine;
    uint32_t wid = engine.create("stateful");
    engine.join(wid, 10);

    nlohmann::json val = {{"health", 100}, {"pos", {1, 2, 3}}};
    REQUIRE(engine.set_state(wid, "player-10", val, 10));

    auto result = engine.get_state(wid, "player-10", 10);
    REQUIRE(result.has_value());
    REQUIRE(result.value() == val);
}

TEST_CASE("WorldEngine get_state for non-existing world", "[world]") {
    WorldEngine engine;
    auto result = engine.get_state(99999, "key", 1);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("WorldEngine state isolation between worlds", "[world]") {
    WorldEngine engine;
    uint32_t w1 = engine.create("world-1");
    uint32_t w2 = engine.create("world-2");
    engine.join(w1, 1);
    engine.join(w2, 1);

    engine.set_state(w1, "shared-key", "value-in-w1", 1);
    engine.set_state(w2, "shared-key", "value-in-w2", 1);

    auto v1 = engine.get_state(w1, "shared-key", 1);
    auto v2 = engine.get_state(w2, "shared-key", 1);

    REQUIRE(v1.has_value());
    REQUIRE(v2.has_value());
    REQUIRE(v1.value() == "value-in-w1");
    REQUIRE(v2.value() == "value-in-w2");
}

TEST_CASE("WorldEngine snapshot captures world data", "[world]") {
    WorldEngine engine;
    nlohmann::json meta = {{"theme", "forest"}};
    uint32_t wid = engine.create("snap-world", meta);
    engine.join(wid, 5);
    engine.set_state(wid, "score", 42, 5);

    auto snap = engine.snapshot(wid);
    REQUIRE(snap.contains("name"));
    REQUIRE(snap["name"] == "snap-world");
    REQUIRE(snap.contains("metadata"));
    REQUIRE(snap["metadata"]["theme"] == "forest");
}

TEST_CASE("WorldEngine restore from snapshot", "[world]") {
    WorldEngine engine;
    nlohmann::json meta = {{"biome", "desert"}};
    uint32_t original = engine.create("original", meta);
    engine.join(original, 7);
    engine.set_state(original, "temp", 45, 7);

    auto snap = engine.snapshot(original);
    uint32_t restored = engine.restore(snap);

    REQUIRE(restored != 0);
    REQUIRE(restored != original);
    REQUIRE(engine.exists(restored));
}

TEST_CASE("WorldEngine exists returns correct results", "[world]") {
    WorldEngine engine;
    uint32_t wid = engine.create("check-exists");

    REQUIRE(engine.exists(wid));
    engine.destroy(wid);
    REQUIRE_FALSE(engine.exists(wid));
    REQUIRE_FALSE(engine.exists(99999));
}
