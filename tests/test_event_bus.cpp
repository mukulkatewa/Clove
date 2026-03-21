#include <catch2/catch_test_macros.hpp>
#include <clove/event_bus.hpp>

using namespace clove;

TEST_CASE("EventBus subscribe and poll", "[events]") {
    EventBus bus;
    bus.subscribe(1, KernelEventType::CUSTOM);
    bus.emit(KernelEventType::CUSTOM, {{"msg", "hello"}}, 0);

    auto events = bus.poll(1);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data["msg"] == "hello");
}

TEST_CASE("EventBus only delivers to subscribers", "[events]") {
    EventBus bus;
    bus.subscribe(1, KernelEventType::CUSTOM);
    // Agent 2 not subscribed
    bus.emit(KernelEventType::CUSTOM, {}, 0);

    REQUIRE(bus.poll(1).size() == 1);
    REQUIRE(bus.poll(2).empty());
}

TEST_CASE("EventBus subscribe_all gets everything", "[events]") {
    EventBus bus;
    bus.subscribe_all(1);

    bus.emit(KernelEventType::AGENT_SPAWNED, {}, 0);
    bus.emit(KernelEventType::CUSTOM, {}, 0);
    bus.emit(KernelEventType::STATE_CHANGED, {}, 0);

    REQUIRE(bus.poll(1).size() == 3);
}

TEST_CASE("EventBus unsubscribe stops delivery", "[events]") {
    EventBus bus;
    bus.subscribe(1, KernelEventType::CUSTOM);
    bus.unsubscribe(1, KernelEventType::CUSTOM);

    bus.emit(KernelEventType::CUSTOM, {}, 0);
    REQUIRE(bus.poll(1).empty());
}

TEST_CASE("EventBus poll drains queue", "[events]") {
    EventBus bus;
    bus.subscribe(1, KernelEventType::CUSTOM);

    for (int i = 0; i < 5; i++) {
        bus.emit(KernelEventType::CUSTOM, {{"seq", i}}, 0);
    }

    auto batch1 = bus.poll(1, 3);
    REQUIRE(batch1.size() == 3);

    auto batch2 = bus.poll(1);
    REQUIRE(batch2.size() == 2);
}

TEST_CASE("EventBus has_events", "[events]") {
    EventBus bus;
    bus.subscribe(1, KernelEventType::CUSTOM);

    REQUIRE_FALSE(bus.has_events(1));
    bus.emit(KernelEventType::CUSTOM, {}, 0);
    REQUIRE(bus.has_events(1));
}
