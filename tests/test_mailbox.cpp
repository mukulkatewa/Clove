#include <catch2/catch_test_macros.hpp>
#include <clove/mailbox.hpp>

using namespace clove;

TEST_CASE("Mailbox register and send by ID", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(1, "alice");
    mb.register_agent(2, "bob");

    REQUIRE(mb.send(1, 2, "hello bob"));

    auto msgs = mb.receive(2);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].content == "hello bob");
    REQUIRE(msgs[0].from_agent_id == 1);
}

TEST_CASE("Mailbox send by name", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(1, "alice");
    mb.register_agent(2, "bob");

    REQUIRE(mb.send_by_name(1, "bob", "hi"));

    auto msgs = mb.receive(2);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].content == "hi");
}

TEST_CASE("Mailbox resolve name to ID", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(42, "agent_x");
    REQUIRE(mb.resolve("agent_x") == 42);
    REQUIRE(mb.resolve("unknown") == 0);
}

TEST_CASE("Mailbox broadcast sends to all except sender", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(1, "a");
    mb.register_agent(2, "b");
    mb.register_agent(3, "c");

    mb.broadcast(1, "broadcast msg");

    // Sender should NOT receive
    REQUIRE(mb.receive(1).empty());
    // Others should receive
    REQUIRE(mb.receive(2).size() == 1);
    REQUIRE(mb.receive(3).size() == 1);
}

TEST_CASE("Mailbox receive drains queue", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(1, "a");
    mb.register_agent(2, "b");

    mb.send(1, 2, "msg1");
    mb.send(1, 2, "msg2");
    mb.send(1, 2, "msg3");

    auto msgs = mb.receive(2, 2); // max 2
    REQUIRE(msgs.size() == 2);

    msgs = mb.receive(2);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].content == "msg3");
}

TEST_CASE("Mailbox has_messages", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(1, "a");
    mb.register_agent(2, "b");

    REQUIRE_FALSE(mb.has_messages(2));
    mb.send(1, 2, "test");
    REQUIRE(mb.has_messages(2));
}

TEST_CASE("Mailbox unregister cleans up", "[mailbox]") {
    AgentMailboxRegistry mb;
    mb.register_agent(1, "a");
    mb.unregister(1);
    REQUIRE(mb.resolve("a") == 0);
}
