#include <catch2/catch_test_macros.hpp>
#include <clove/database.hpp>
#include <clove/audit_store.hpp>
#include <clove/state_store_db.hpp>
#include <clove/audit_log.hpp>
#include <cstdio>

using namespace clove;

static const char* TEST_DB = "/tmp/clove_test.db";

struct DbFixture {
    Database db{TEST_DB};
    DbFixture() { std::remove(TEST_DB); db.open(); }
    ~DbFixture() { db.close(); std::remove(TEST_DB); }
};

TEST_CASE("Database opens and creates schema", "[db]") {
    DbFixture f;
    REQUIRE(f.db.is_open());
}

TEST_CASE("Database exec runs SQL", "[db]") {
    DbFixture f;
    REQUIRE(f.db.exec("SELECT 1"));
}

TEST_CASE("StateStoreDb store and fetch", "[db]") {
    DbFixture f;
    StateStoreDb store(f.db);

    REQUIRE(store.store("key1", nlohmann::json("hello"), 1, "global"));
    auto val = store.fetch("key1");
    REQUIRE(val.has_value());
    REQUIRE(*val == "hello");
}

TEST_CASE("StateStoreDb fetch missing returns nullopt", "[db]") {
    DbFixture f;
    StateStoreDb store(f.db);
    REQUIRE_FALSE(store.fetch("nope").has_value());
}

TEST_CASE("StateStoreDb erase", "[db]") {
    DbFixture f;
    StateStoreDb store(f.db);
    store.store("k", nlohmann::json(1), 1, "global");
    REQUIRE(store.erase("k"));
    REQUIRE_FALSE(store.fetch("k").has_value());
}

TEST_CASE("StateStoreDb keys with prefix", "[db]") {
    DbFixture f;
    StateStoreDb store(f.db);
    store.store("user:1", nlohmann::json(1), 1, "global");
    store.store("user:2", nlohmann::json(2), 1, "global");
    store.store("config:x", nlohmann::json(3), 1, "global");

    auto k = store.keys("user:");
    REQUIRE(k.size() == 2);
}

TEST_CASE("StateStoreDb load_all", "[db]") {
    DbFixture f;
    StateStoreDb store(f.db);
    store.store("a", nlohmann::json(1), 10, "global");
    store.store("b", nlohmann::json(2), 20, "agent");

    auto all = store.load_all();
    REQUIRE(all.size() == 2);
}

TEST_CASE("StateStoreDb count", "[db]") {
    DbFixture f;
    StateStoreDb store(f.db);
    REQUIRE(store.count() == 0);
    store.store("x", nlohmann::json(1), 1, "global");
    REQUIRE(store.count() == 1);
}

TEST_CASE("AuditStore store and query", "[db]") {
    DbFixture f;
    AuditStore store(f.db);

    AuditLogEntry entry;
    entry.category = AuditCategory::SECURITY;
    entry.event_type = "LOGIN";
    entry.agent_id = 42;
    entry.agent_name = "test_agent";
    entry.details = {{"ip", "10.0.0.1"}};
    entry.success = true;

    REQUIRE(store.store(entry));

    auto results = store.query();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].event_type == "LOGIN");
    REQUIRE(results[0].agent_id == 42);
}

TEST_CASE("AuditStore batch insert", "[db]") {
    DbFixture f;
    AuditStore store(f.db);

    std::vector<AuditLogEntry> entries;
    for (int i = 0; i < 100; i++) {
        AuditLogEntry e;
        e.category = AuditCategory::SYSCALL;
        e.event_type = "CALL";
        e.agent_id = 1;
        e.agent_name = "agent";
        e.details = {{"seq", i}};
        e.success = true;
        entries.push_back(e);
    }

    REQUIRE(store.store_batch(entries));
    REQUIRE(store.count() == 100);
}

TEST_CASE("AuditStore filter by category", "[db]") {
    DbFixture f;
    AuditStore store(f.db);

    AuditLogEntry e1;
    e1.category = AuditCategory::SECURITY; e1.event_type = "A"; e1.agent_id = 1;
    e1.agent_name = "x"; e1.details = {}; e1.success = true;
    store.store(e1);

    AuditLogEntry e2;
    e2.category = AuditCategory::IPC; e2.event_type = "B"; e2.agent_id = 1;
    e2.agent_name = "x"; e2.details = {}; e2.success = true;
    store.store(e2);

    AuditCategory cat = AuditCategory::SECURITY;
    auto results = store.query(&cat);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].event_type == "A");
}
