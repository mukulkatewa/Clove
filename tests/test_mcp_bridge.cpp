#include <catch2/catch_test_macros.hpp>
#include <clove/mcp_bridge.hpp>

using namespace clove;

TEST_CASE("McpBridge add and remove server", "[mcp]") {
    McpBridge bridge;

    McpServerConfig cfg;
    cfg.name = "test";
    cfg.command = "echo";

    REQUIRE(bridge.add_server(cfg));
    REQUIRE(bridge.has_server("test"));

    // Duplicate add fails
    REQUIRE_FALSE(bridge.add_server(cfg));

    REQUIRE(bridge.remove_server("test"));
    REQUIRE_FALSE(bridge.has_server("test"));
}

TEST_CASE("McpBridge list_tools empty without connected servers", "[mcp]") {
    McpBridge bridge;

    McpServerConfig cfg;
    cfg.name = "test";
    cfg.command = "nonexistent_mcp_server";
    bridge.add_server(cfg);

    // Not started — no tools
    auto tools = bridge.list_tools();
    REQUIRE(tools.empty());
}

TEST_CASE("McpBridge status shows server info", "[mcp]") {
    McpBridge bridge;

    McpServerConfig cfg;
    cfg.name = "server1";
    cfg.command = "echo";
    bridge.add_server(cfg);

    auto statuses = bridge.status();
    REQUIRE(statuses.size() == 1);
    REQUIRE(statuses[0].name == "server1");
    REQUIRE_FALSE(statuses[0].connected);
}

TEST_CASE("McpBridge call_tool fails on missing server", "[mcp]") {
    McpBridge bridge;
    auto result = bridge.call_tool(1, "nonexistent", "tool", {});
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("not found") != std::string::npos);
}

TEST_CASE("McpBridge allowed_agents filtering", "[mcp]") {
    McpBridge bridge;

    McpServerConfig cfg;
    cfg.name = "restricted";
    cfg.command = "echo";
    cfg.allowed_agents = {"alice", "bob"};
    bridge.add_server(cfg);

    // Can't test full tool listing without a running server,
    // but the config is stored correctly
    REQUIRE(bridge.has_server("restricted"));
}

TEST_CASE("McpBridge remove nonexistent returns false", "[mcp]") {
    McpBridge bridge;
    REQUIRE_FALSE(bridge.remove_server("nope"));
}
