#include <catch2/catch_test_macros.hpp>
#include <clove/permissions.hpp>
#include <clove/permissions_store.hpp>

using namespace clove;

TEST_CASE("AgentPermissions default values", "[permissions]") {
    AgentPermissions p;
    REQUIRE_FALSE(p.can_exec);
    REQUIRE(p.can_read);
    REQUIRE(p.can_write);
    REQUIRE(p.can_think);
    REQUIRE_FALSE(p.can_spawn);
    REQUIRE_FALSE(p.can_http);
}

TEST_CASE("AgentPermissions from level SANDBOXED", "[permissions]") {
    auto p = AgentPermissions::from_level(PermissionLevel::SANDBOXED);
    REQUIRE_FALSE(p.can_exec);
    REQUIRE_FALSE(p.can_spawn);
    REQUIRE_FALSE(p.can_http);
}

TEST_CASE("AgentPermissions from level UNRESTRICTED", "[permissions]") {
    auto p = AgentPermissions::from_level(PermissionLevel::UNRESTRICTED);
    REQUIRE(p.can_exec);
    REQUIRE(p.can_spawn);
    REQUIRE(p.can_http);
}

TEST_CASE("AgentPermissions JSON roundtrip", "[permissions]") {
    AgentPermissions original;
    original.can_exec = true;
    original.can_http = true;
    original.allowed_domains = {"example.com", "*.api.com"};

    auto j = original.to_json();
    auto restored = AgentPermissions::from_json(j);

    REQUIRE(restored.can_exec == true);
    REQUIRE(restored.can_http == true);
    REQUIRE(restored.allowed_domains.size() == 2);
}

// NOTE: LLM token/cost tracking is now in AgentBudget, tested in test_budget.cpp

TEST_CASE("PermissionsStore get_or_create", "[permissions]") {
    PermissionsStore store;
    auto& p = store.get_or_create(1);
    p.can_exec = true;

    REQUIRE(store.exists(1));
    REQUIRE(store.get_or_create(1).can_exec);
}

TEST_CASE("PermissionsStore set_level", "[permissions]") {
    PermissionsStore store;
    store.set_level(1, PermissionLevel::SANDBOXED);
    auto& p = store.get_or_create(1);
    REQUIRE_FALSE(p.can_exec);
    REQUIRE_FALSE(p.can_http);
}

TEST_CASE("PermissionsStore remove", "[permissions]") {
    PermissionsStore store;
    store.get_or_create(1);
    REQUIRE(store.exists(1));
    store.remove(1);
    REQUIRE_FALSE(store.exists(1));
}

TEST_CASE("PermissionChecker path_matches", "[permissions]") {
    REQUIRE(PermissionChecker::path_matches("/tmp/foo.txt", "/tmp/*"));
    REQUIRE_FALSE(PermissionChecker::path_matches("/etc/passwd", "/tmp/*"));
}

TEST_CASE("PermissionChecker extract_domain", "[permissions]") {
    REQUIRE(PermissionChecker::extract_domain("https://api.example.com/v1/data") == "api.example.com");
    REQUIRE(PermissionChecker::extract_domain("http://localhost:8080/test") == "localhost");
}
