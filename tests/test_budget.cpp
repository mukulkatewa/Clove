#include <catch2/catch_test_macros.hpp>
#include <clove/permissions.hpp>
#include <clove/permissions_store.hpp>
#include <chrono>

using namespace clove;

static uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// AgentBudget struct tests
// ---------------------------------------------------------------------------

TEST_CASE("AgentBudget defaults are unlimited", "[budget]") {
    AgentBudget b;
    REQUIRE(b.max_tokens == 0);
    REQUIRE(b.max_steps == 0);
    REQUIRE(b.max_time_ms == 0);
    REQUIRE(b.max_cost_usd == 0.0);
    REQUIRE_FALSE(b.kill_on_exceeded);

    // All checks pass when unlimited
    REQUIRE(b.check_tokens());
    REQUIRE(b.check_steps());
    REQUIRE(b.check_time(now_ms()));
    REQUIRE(b.check_cost());
    REQUIRE_FALSE(b.any_exceeded(now_ms()));
}

TEST_CASE("AgentBudget token tracking", "[budget]") {
    AgentBudget b;
    b.max_tokens = 1000;

    REQUIRE(b.check_tokens());
    REQUIRE(b.check_tokens(500));  // 0 + 500 <= 1000
    REQUIRE(b.check_tokens(1000)); // 0 + 1000 <= 1000

    b.record_tokens(600);
    REQUIRE(b.tokens_used == 600);
    REQUIRE(b.check_tokens());       // 600 <= 1000
    REQUIRE(b.check_tokens(300));    // 600 + 300 <= 1000
    REQUIRE_FALSE(b.check_tokens(500)); // 600 + 500 > 1000

    b.record_tokens(500);
    REQUIRE(b.tokens_used == 1100);
    REQUIRE_FALSE(b.check_tokens()); // 1100 > 1000
}

TEST_CASE("AgentBudget step tracking", "[budget]") {
    AgentBudget b;
    b.max_steps = 3;

    REQUIRE(b.check_steps());
    b.record_step(); // 1
    b.record_step(); // 2
    b.record_step(); // 3
    REQUIRE(b.check_steps()); // 3 <= 3

    b.record_step(); // 4
    REQUIRE_FALSE(b.check_steps()); // 4 > 3
}

TEST_CASE("AgentBudget time tracking", "[budget]") {
    AgentBudget b;
    b.max_time_ms = 100;

    // Timer not started — always passes
    REQUIRE(b.check_time(now_ms()));

    uint64_t start = now_ms();
    b.start_timer(start);

    // Within budget
    REQUIRE(b.check_time(start + 50));  // 50ms < 100ms
    REQUIRE(b.check_time(start + 100)); // 100ms <= 100ms

    // Exceeded
    REQUIRE_FALSE(b.check_time(start + 101)); // 101ms > 100ms
}

TEST_CASE("AgentBudget cost tracking", "[budget]") {
    AgentBudget b;
    b.max_cost_usd = 1.50;

    REQUIRE(b.check_cost());
    REQUIRE(b.check_cost(1.0)); // 0 + 1.0 <= 1.50

    b.record_cost(0.75);
    REQUIRE(b.check_cost());       // 0.75 <= 1.50
    REQUIRE(b.check_cost(0.50));   // 0.75 + 0.50 <= 1.50
    REQUIRE_FALSE(b.check_cost(1.0)); // 0.75 + 1.0 > 1.50
}

TEST_CASE("AgentBudget any_exceeded and exceeded_type", "[budget]") {
    AgentBudget b;
    b.max_steps = 2;
    b.max_tokens = 500;

    uint64_t now = now_ms();
    REQUIRE_FALSE(b.any_exceeded(now));

    b.record_step();
    b.record_step();
    b.record_step(); // exceeded
    REQUIRE(b.any_exceeded(now));
    REQUIRE(b.exceeded_type(now) == "step");
}

TEST_CASE("AgentBudget reset", "[budget]") {
    AgentBudget b;
    b.max_tokens = 100;
    b.max_steps = 5;
    b.record_tokens(50);
    b.record_step();
    b.record_step();
    b.record_cost(0.10);
    b.start_timer(now_ms());

    b.reset();
    REQUIRE(b.tokens_used == 0);
    REQUIRE(b.steps_taken == 0);
    REQUIRE(b.cost_usd == 0.0);
    REQUIRE(b.started_at_ms == 0);
    // Limits are preserved
    REQUIRE(b.max_tokens == 100);
    REQUIRE(b.max_steps == 5);
}

TEST_CASE("AgentBudget JSON roundtrip", "[budget]") {
    AgentBudget original;
    original.max_tokens = 5000;
    original.max_steps = 100;
    original.max_time_ms = 60000;
    original.max_cost_usd = 2.50;
    original.kill_on_exceeded = true;
    original.tokens_used = 1200;
    original.steps_taken = 30;
    original.cost_usd = 0.45;

    auto j = original.to_json();
    auto restored = AgentBudget::from_json(j);

    REQUIRE(restored.max_tokens == 5000);
    REQUIRE(restored.max_steps == 100);
    REQUIRE(restored.max_time_ms == 60000);
    REQUIRE(restored.max_cost_usd == 2.50);
    REQUIRE(restored.kill_on_exceeded == true);
    REQUIRE(restored.tokens_used == 1200);
    REQUIRE(restored.steps_taken == 30);
    REQUIRE(restored.cost_usd == 0.45);
}

// ---------------------------------------------------------------------------
// PermissionsStore budget tests
// ---------------------------------------------------------------------------

TEST_CASE("PermissionsStore budget get_or_create", "[budget]") {
    PermissionsStore store;

    auto& b = store.get_or_create_budget(42);
    REQUIRE(b.max_tokens == 0); // defaults

    b.max_tokens = 1000;
    b.record_tokens(500);

    // Same agent returns same budget
    auto& b2 = store.get_or_create_budget(42);
    REQUIRE(b2.tokens_used == 500);
}

TEST_CASE("PermissionsStore set_budget", "[budget]") {
    PermissionsStore store;

    AgentBudget b;
    b.max_steps = 10;
    b.max_cost_usd = 5.0;
    store.set_budget(1, b);

    auto& stored = store.get_or_create_budget(1);
    REQUIRE(stored.max_steps == 10);
    REQUIRE(stored.max_cost_usd == 5.0);
}

TEST_CASE("PermissionsStore for_each_budget", "[budget]") {
    PermissionsStore store;

    store.get_or_create_budget(1).max_steps = 10;
    store.get_or_create_budget(2).max_steps = 20;
    store.get_or_create_budget(3).max_steps = 30;

    int count = 0;
    store.for_each_budget([&](uint32_t, AgentBudget&) { count++; });
    REQUIRE(count == 3);
}

TEST_CASE("PermissionsStore remove_budget", "[budget]") {
    PermissionsStore store;

    store.get_or_create_budget(1).max_steps = 10;
    store.remove_budget(1);

    // Should get a fresh default budget
    auto& b = store.get_or_create_budget(1);
    REQUIRE(b.max_steps == 0);
}
