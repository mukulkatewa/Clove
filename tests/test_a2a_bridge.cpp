#include <catch2/catch_test_macros.hpp>
#include <clove/a2a_bridge.hpp>

using namespace clove;

TEST_CASE("A2aBridge register and unregister agent", "[a2a]") {
    A2aBridge bridge;

    bridge.register_agent("planner", "Plans tasks", {"plan", "schedule"});
    auto card = bridge.agent_card();
    REQUIRE(card.contains("agents"));
    REQUIRE(card["agents"].size() == 1);

    bridge.unregister_agent("planner");
    card = bridge.agent_card();
    REQUIRE(card["agents"].size() == 0);
}

TEST_CASE("A2aBridge agent_card contains registered agents", "[a2a]") {
    A2aBridge bridge;

    bridge.register_agent("coder", "Writes code", {"code", "review"});
    bridge.register_agent("tester", "Tests code", {"test"});

    auto card = bridge.agent_card();
    REQUIRE(card.contains("agents"));
    REQUIRE(card["agents"].size() == 2);
}

TEST_CASE("A2aBridge has_messages false for unregistered agent", "[a2a]") {
    A2aBridge bridge;
    REQUIRE_FALSE(bridge.has_messages("nobody"));
}

TEST_CASE("A2aBridge receive empty for agent with no messages", "[a2a]") {
    A2aBridge bridge;
    bridge.register_agent("idle", "Does nothing");

    auto msgs = bridge.receive("idle");
    REQUIRE(msgs.empty());
}

TEST_CASE("A2aBridge send to unreachable URL fails gracefully", "[a2a]") {
    A2aBridge bridge;

    auto result = bridge.send(
        "http://127.0.0.1:1",  // unreachable port
        "local-agent",
        "hello",
        {{"priority", "low"}}
    );

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.error.empty());
}
