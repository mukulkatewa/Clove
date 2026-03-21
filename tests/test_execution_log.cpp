#include <catch2/catch_test_macros.hpp>
#include <clove/execution_log.hpp>

using namespace clove;

TEST_CASE("ExecutionLogger starts idle", "[replay]") {
    ExecutionLogger logger;
    REQUIRE(logger.recording_state() == RecordingState::IDLE);
    REQUIRE(logger.entry_count() == 0);
}

TEST_CASE("ExecutionLogger start/stop recording", "[replay]") {
    ExecutionLogger logger;
    REQUIRE(logger.start_recording());
    REQUIRE(logger.recording_state() == RecordingState::RECORDING);
    REQUIRE(logger.stop_recording());
    REQUIRE(logger.recording_state() == RecordingState::IDLE);
}

TEST_CASE("ExecutionLogger records syscalls when active", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    logger.record(1, SyscallOp::SYS_STORE, "payload", "response", 100, true);
    logger.record(1, SyscallOp::SYS_FETCH, "p2", "r2", 50, true);

    REQUIRE(logger.entry_count() == 2);

    auto entries = logger.get_entries();
    REQUIRE(entries[0].opcode == SyscallOp::SYS_STORE);
    REQUIRE(entries[0].duration_us == 100);
    REQUIRE(entries[1].opcode == SyscallOp::SYS_FETCH);
}

TEST_CASE("ExecutionLogger ignores NOOP", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();

    logger.record(1, SyscallOp::SYS_NOOP, "", "", 10, true);
    REQUIRE(logger.entry_count() == 0);
}

TEST_CASE("ExecutionLogger skips when idle", "[replay]") {
    ExecutionLogger logger;
    // Not recording — should silently skip
    logger.record(1, SyscallOp::SYS_STORE, "x", "y", 10, true);
    REQUIRE(logger.entry_count() == 0);
}

TEST_CASE("ExecutionLogger clear", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();
    logger.record(1, SyscallOp::SYS_STORE, "", "", 10, true);
    logger.stop_recording();
    logger.clear();
    REQUIRE(logger.entry_count() == 0);
}

TEST_CASE("ExecutionLogger max entries cap", "[replay]") {
    ExecutionLogger logger;
    RecordingConfig cfg;
    cfg.max_entries = 5;
    logger.start_recording(cfg);

    for (int i = 0; i < 10; i++) {
        logger.record(1, SyscallOp::SYS_STORE, "", "", 10, true);
    }
    REQUIRE(logger.entry_count() == 5);
}

TEST_CASE("ExecutionLogger pause stops recording", "[replay]") {
    ExecutionLogger logger;
    logger.start_recording();
    logger.record(1, SyscallOp::SYS_STORE, "", "", 10, true);
    REQUIRE(logger.pause_recording());
    logger.record(1, SyscallOp::SYS_STORE, "", "", 10, true);
    REQUIRE(logger.entry_count() == 1); // Only 1 from before pause
}
