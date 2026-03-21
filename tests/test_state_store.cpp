#include <catch2/catch_test_macros.hpp>
#include <clove/state_store.hpp>
#include <thread>

using namespace clove;
using json = nlohmann::json;

TEST_CASE("StateStore basic store and fetch", "[state]") {
    StateStore ss;
    REQUIRE(ss.store("key1", json("value1"), 1));
    auto val = ss.fetch("key1", 1);
    REQUIRE(val.has_value());
    REQUIRE(*val == "value1");
}

TEST_CASE("StateStore fetch missing key returns nullopt", "[state]") {
    StateStore ss;
    REQUIRE_FALSE(ss.fetch("nonexistent", 1).has_value());
}

TEST_CASE("StateStore erase removes key", "[state]") {
    StateStore ss;
    ss.store("key1", json(42), 1);
    REQUIRE(ss.erase("key1", 1));
    REQUIRE_FALSE(ss.fetch("key1", 1).has_value());
}

TEST_CASE("StateStore keys with prefix filter", "[state]") {
    StateStore ss;
    ss.store("user:alice", json(1), 1);
    ss.store("user:bob", json(2), 1);
    ss.store("config:debug", json(true), 1);

    auto user_keys = ss.keys("user:", 1);
    REQUIRE(user_keys.size() == 2);

    auto all_keys = ss.keys("", 1);
    REQUIRE(all_keys.size() == 3);
}

TEST_CASE("StateStore TTL eviction", "[state]") {
    StateStore ss;
    ss.store("temp", json("expires"), 1, "global", 50); // 50ms TTL

    REQUIRE(ss.fetch("temp", 1).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ss.evict_expired();
    REQUIRE_FALSE(ss.fetch("temp", 1).has_value());
}

TEST_CASE("StateStore agent scope isolation", "[state]") {
    StateStore ss;
    ss.store("private", json("secret"), 1, "agent");

    // Owner can fetch
    REQUIRE(ss.fetch("private", 1).has_value());
    // Other agent cannot
    REQUIRE_FALSE(ss.fetch("private", 2).has_value());
}

TEST_CASE("StateStore global scope shared", "[state]") {
    StateStore ss;
    ss.store("shared", json("public"), 1, "global");
    REQUIRE(ss.fetch("shared", 2).has_value());
}

TEST_CASE("StateStore size tracking", "[state]") {
    StateStore ss;
    REQUIRE(ss.size() == 0);
    ss.store("a", json(1), 1);
    ss.store("b", json(2), 1);
    REQUIRE(ss.size() == 2);
    ss.erase("a", 1);
    REQUIRE(ss.size() == 1);
}

TEST_CASE("StateStore overwrite existing key", "[state]") {
    StateStore ss;
    ss.store("key", json("old"), 1);
    ss.store("key", json("new"), 1);
    REQUIRE(*ss.fetch("key", 1) == "new");
    REQUIRE(ss.size() == 1);
}
