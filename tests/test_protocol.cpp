#include <catch2/catch_test_macros.hpp>
#include <clove/protocol.hpp>

using namespace clove;

TEST_CASE("Message header is 17 bytes", "[protocol]") {
    REQUIRE(sizeof(MessageHeader) == 17);
}

TEST_CASE("Message::create builds correct header", "[protocol]") {
    auto msg = Message::create(42, SyscallOp::SYS_NOOP, "hello");
    REQUIRE(msg.header.magic == PROTOCOL_MAGIC);
    REQUIRE(msg.header.agent_id == 42);
    REQUIRE(msg.header.opcode == static_cast<uint8_t>(SyscallOp::SYS_NOOP));
    REQUIRE(msg.header.payload_size == 5);
    REQUIRE(msg.payload_str() == "hello");
}

TEST_CASE("Message serialize/deserialize roundtrip", "[protocol]") {
    auto original = Message::create(100, SyscallOp::SYS_STORE, "test payload");
    auto bytes = original.serialize();

    auto restored = Message::deserialize(bytes);
    REQUIRE(restored.has_value());
    REQUIRE(restored->header.agent_id == 100);
    REQUIRE(restored->opcode() == SyscallOp::SYS_STORE);
    REQUIRE(restored->payload_str() == "test payload");
}

TEST_CASE("Message deserialize rejects bad magic", "[protocol]") {
    auto msg = Message::create(1, SyscallOp::SYS_NOOP);
    auto bytes = msg.serialize();
    bytes[0] = 0xFF; // Corrupt magic
    REQUIRE_FALSE(Message::deserialize(bytes).has_value());
}

TEST_CASE("Message deserialize rejects truncated data", "[protocol]") {
    auto msg = Message::create(1, SyscallOp::SYS_NOOP, "data");
    auto bytes = msg.serialize();
    bytes.resize(10); // Truncate
    REQUIRE_FALSE(Message::deserialize(bytes).has_value());
}

TEST_CASE("Empty payload message works", "[protocol]") {
    auto msg = Message::create(0, SyscallOp::SYS_NOOP);
    REQUIRE(msg.payload.empty());
    REQUIRE(msg.payload_str().empty());

    auto bytes = msg.serialize();
    auto restored = Message::deserialize(bytes);
    REQUIRE(restored.has_value());
    REQUIRE(restored->payload.empty());
}

TEST_CASE("opcode_to_string returns names", "[protocol]") {
    REQUIRE(std::string(opcode_to_string(SyscallOp::SYS_NOOP)) == "SYS_NOOP");
    REQUIRE(std::string(opcode_to_string(SyscallOp::SYS_HELLO)) == "SYS_HELLO");
}
