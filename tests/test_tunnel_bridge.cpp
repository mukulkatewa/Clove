#include <catch2/catch_test_macros.hpp>
#include <clove/tunnel_bridge.hpp>

using namespace clove;

TEST_CASE("TunnelBridge default state is DISCONNECTED", "[tunnel]") {
    TunnelBridge bridge;
    REQUIRE(bridge.state() == TunnelState::DISCONNECTED);
}

TEST_CASE("TunnelBridge connect with valid config", "[tunnel]") {
    TunnelBridge bridge;
    TunnelConfig cfg;
    cfg.relay_url = "wss://relay.example.com";
    cfg.machine_id = "machine-001";
    cfg.machine_token = "token-abc";

    bool ok = bridge.connect(cfg);
    REQUIRE(ok);
    REQUIRE(bridge.state() == TunnelState::CONNECTED);
    REQUIRE_FALSE(bridge.session_id().empty());
}

TEST_CASE("TunnelBridge connect with empty relay_url fails", "[tunnel]") {
    TunnelBridge bridge;
    TunnelConfig cfg;
    cfg.relay_url = "";
    cfg.machine_id = "machine-001";

    bool ok = bridge.connect(cfg);
    REQUIRE_FALSE(ok);
    REQUIRE(bridge.state() == TunnelState::ERROR);
}

TEST_CASE("TunnelBridge disconnect clears session", "[tunnel]") {
    TunnelBridge bridge;
    TunnelConfig cfg;
    cfg.relay_url = "wss://relay.example.com";
    cfg.machine_id = "machine-001";
    cfg.machine_token = "token-abc";

    bridge.connect(cfg);
    bridge.disconnect();

    REQUIRE(bridge.state() == TunnelState::DISCONNECTED);
    REQUIRE(bridge.session_id().empty());
}

TEST_CASE("TunnelBridge status_json contains expected fields", "[tunnel]") {
    TunnelBridge bridge;
    TunnelConfig cfg;
    cfg.relay_url = "wss://relay.example.com";
    cfg.machine_id = "machine-42";
    cfg.machine_token = "tok";

    bridge.connect(cfg);
    auto j = bridge.status_json();

    REQUIRE(j.contains("state"));
    REQUIRE(j.contains("relay_url"));
    REQUIRE(j.contains("machine_id"));
    REQUIRE(j.contains("session_id"));
    REQUIRE(j["relay_url"] == "wss://relay.example.com");
    REQUIRE(j["machine_id"] == "machine-42");
}

TEST_CASE("TunnelBridge list_remotes empty without relay", "[tunnel]") {
    TunnelBridge bridge;
    auto remotes = bridge.list_remotes();
    REQUIRE(remotes.empty());
}

TEST_CASE("TunnelBridge update_config changes relay_url", "[tunnel]") {
    TunnelBridge bridge;
    TunnelConfig cfg;
    cfg.relay_url = "wss://old.example.com";
    cfg.machine_id = "m1";

    bridge.update_config(cfg);
    REQUIRE(bridge.config().relay_url == "wss://old.example.com");

    TunnelConfig cfg2;
    cfg2.relay_url = "wss://new.example.com";
    cfg2.machine_id = "m1";

    bridge.update_config(cfg2);
    REQUIRE(bridge.config().relay_url == "wss://new.example.com");
}

TEST_CASE("TunnelBridge connect-disconnect-reconnect cycle", "[tunnel]") {
    TunnelBridge bridge;
    TunnelConfig cfg;
    cfg.relay_url = "wss://relay.example.com";
    cfg.machine_id = "machine-001";
    cfg.machine_token = "token-abc";

    REQUIRE(bridge.connect(cfg));
    REQUIRE(bridge.state() == TunnelState::CONNECTED);
    auto first_session = bridge.session_id();

    bridge.disconnect();
    REQUIRE(bridge.state() == TunnelState::DISCONNECTED);

    REQUIRE(bridge.connect(cfg));
    REQUIRE(bridge.state() == TunnelState::CONNECTED);
    REQUIRE_FALSE(bridge.session_id().empty());
}
