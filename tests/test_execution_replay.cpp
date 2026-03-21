#include <catch2/catch_test_macros.hpp>
#include <clove/execution_log.hpp>

using namespace clove;

// SYS_STORE is always recorded (not filtered like SYS_NOOP)
static constexpr auto RECORDABLE_OP = SyscallOp::SYS_STORE;

TEST_CASE("ExecutionLogger records entries during RECORDING", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();
    REQUIRE(logger.recording_state() == RecordingState::RECORDING);

    logger.record(1, RECORDABLE_OP, "ping", "pong", 100, true);
    REQUIRE(logger.entry_count() == 1);
}

TEST_CASE("ExecutionLogger get_entries returns all in order", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    logger.record(1, RECORDABLE_OP, "a", "r1", 10, true);
    logger.record(2, RECORDABLE_OP, "b", "r2", 20, true);
    logger.record(3, RECORDABLE_OP, "c", "r3", 30, false);

    auto entries = logger.get_entries();
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0].payload == "a");
    REQUIRE(entries[1].payload == "b");
    REQUIRE(entries[2].payload == "c");
}

TEST_CASE("ExecutionLogger entry contains correct fields", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    logger.record(42, RECORDABLE_OP, "run ls", "files", 500, true);

    auto entries = logger.get_entries();
    REQUIRE(entries.size() == 1);

    const auto& e = entries[0];
    REQUIRE(e.agent_id == 42);
    REQUIRE(e.opcode == RECORDABLE_OP);
    REQUIRE(e.payload == "run ls");
    REQUIRE(e.response == "files");
    REQUIRE(e.duration_us == 500);
    REQUIRE(e.success == true);
}

TEST_CASE("ExecutionLogger clear removes all entries", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    logger.record(1, RECORDABLE_OP, "x", "y", 1, true);
    logger.record(2, RECORDABLE_OP, "x", "y", 1, true);
    REQUIRE(logger.entry_count() == 2);

    logger.clear();
    REQUIRE(logger.entry_count() == 0);
    REQUIRE(logger.get_entries().empty());
}

TEST_CASE("ExecutionLogger entry_count matches expected", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    for (int i = 0; i < 10; ++i) {
        logger.record(1, RECORDABLE_OP, "p", "r", 1, true);
    }
    REQUIRE(logger.entry_count() == 10);
}

TEST_CASE("ExecutionLogger pause then resume recording", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    logger.record(1, RECORDABLE_OP, "before-pause", "r", 1, true);
    REQUIRE(logger.entry_count() == 1);

    logger.pause_recording();
    REQUIRE(logger.recording_state() == RecordingState::PAUSED);

    // Records during PAUSED should be ignored
    logger.record(1, RECORDABLE_OP, "during-pause", "r", 1, true);
    REQUIRE(logger.entry_count() == 1);

    // Resume by starting again
    logger.start_recording();
    REQUIRE(logger.recording_state() == RecordingState::RECORDING);

    logger.record(1, RECORDABLE_OP, "after-resume", "r", 1, true);
    // start_recording() clears entries, so only the new one
    REQUIRE(logger.entry_count() == 1);
}

TEST_CASE("ExecutionLogger max_entries cap is enforced", "[replay]") {
    ExecutionLogger logger;
    RecordingConfig cfg;
    cfg.max_entries = 5;
    logger.start_recording(cfg);

    for (int i = 0; i < 20; ++i) {
        logger.record(1, RECORDABLE_OP, "p" + std::to_string(i), "r", 1, true);
    }

    REQUIRE(logger.entry_count() <= 5);
}
