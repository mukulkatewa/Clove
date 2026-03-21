#include <catch2/catch_test_macros.hpp>
#include <clove/audit_log.hpp>

using namespace clove;

TEST_CASE("AuditLogger log and query", "[audit]") {
    AuditLogger logger;
    logger.log(AuditCategory::SECURITY, "LOGIN", 1, "agent_a", {{"ip", "10.0.0.1"}});

    auto entries = logger.get_entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].event_type == "LOGIN");
    REQUIRE(entries[0].agent_id == 1);
    REQUIRE(entries[0].details["ip"] == "10.0.0.1");
}

TEST_CASE("AuditLogger filter by category", "[audit]") {
    AuditLogger logger;
    logger.log(AuditCategory::SECURITY, "SEC_EVENT", 1, "a", {});
    logger.log(AuditCategory::IPC, "IPC_EVENT", 2, "b", {});
    logger.log(AuditCategory::SECURITY, "SEC_EVENT2", 3, "c", {});

    AuditCategory cat = AuditCategory::SECURITY;
    auto entries = logger.get_entries(&cat);
    REQUIRE(entries.size() == 2);
}

TEST_CASE("AuditLogger filter by agent_id", "[audit]") {
    AuditLogger logger;
    logger.log(AuditCategory::SECURITY, "E1", 1, "a", {});
    logger.log(AuditCategory::SECURITY, "E2", 2, "b", {});
    logger.log(AuditCategory::SECURITY, "E3", 1, "a", {});

    uint32_t agent = 1;
    auto entries = logger.get_entries(nullptr, &agent);
    REQUIRE(entries.size() == 2);
}

TEST_CASE("AuditLogger since_id pagination", "[audit]") {
    AuditLogger logger;
    for (int i = 0; i < 10; i++) {
        logger.log(AuditCategory::SECURITY, "E", 1, "a", {{"seq", i}});
    }

    auto first5 = logger.get_entries(nullptr, nullptr, 0, 5);
    REQUIRE(first5.size() == 5);

    auto next5 = logger.get_entries(nullptr, nullptr, first5.back().id, 5);
    REQUIRE(next5.size() == 5);
}

TEST_CASE("AuditLogger entry_count", "[audit]") {
    AuditLogger logger;
    REQUIRE(logger.entry_count() == 0);
    logger.log(AuditCategory::SECURITY, "E", 1, "a", {});
    logger.log(AuditCategory::SECURITY, "E", 1, "a", {});
    REQUIRE(logger.entry_count() == 2);
}

TEST_CASE("AuditLogger clear", "[audit]") {
    AuditLogger logger;
    logger.log(AuditCategory::SECURITY, "E", 1, "a", {});
    logger.clear();
    REQUIRE(logger.entry_count() == 0);
}

TEST_CASE("AuditLogger to_json", "[audit]") {
    AuditLogger logger;
    logger.log(AuditCategory::SECURITY, "TEST", 42, "my_agent", {{"key", "val"}});

    auto entries = logger.get_entries();
    auto j = entries[0].to_json();
    REQUIRE(j["event_type"] == "TEST");
    REQUIRE(j["agent_id"] == 42);
}

TEST_CASE("audit_category roundtrip", "[audit]") {
    REQUIRE(audit_category_from_string(audit_category_to_string(AuditCategory::SECURITY)) == AuditCategory::SECURITY);
    REQUIRE(audit_category_from_string(audit_category_to_string(AuditCategory::IPC)) == AuditCategory::IPC);
}
