#include <catch2/catch_test_macros.hpp>
#include <clove/inference_gateway.hpp>

using namespace clove;

TEST_CASE("InferenceGateway disabled by default", "[gateway]") {
    InferenceGateway gw;
    REQUIRE_FALSE(gw.is_enabled());
}

TEST_CASE("InferenceGateway model allowlist", "[gateway]") {
    InferenceGateway gw;
    InferenceGatewayConfig cfg;
    cfg.enabled = true;
    cfg.allowed_models = {"gpt-4", "gpt-4o", "claude-*"};
    gw.configure(cfg);

    REQUIRE(gw.is_model_allowed("gpt-4"));
    REQUIRE(gw.is_model_allowed("gpt-4o"));
    REQUIRE(gw.is_model_allowed("claude-3-opus"));
    REQUIRE_FALSE(gw.is_model_allowed("llama-3"));
}

TEST_CASE("InferenceGateway empty allowlist allows all", "[gateway]") {
    InferenceGateway gw;
    InferenceGatewayConfig cfg;
    cfg.enabled = true;
    cfg.allowed_models = {};
    gw.configure(cfg);

    REQUIRE(gw.is_model_allowed("anything"));
}

TEST_CASE("InferenceGateway cost tracking", "[gateway]") {
    InferenceGateway gw;
    InferenceGatewayConfig cfg;
    cfg.enabled = true;
    cfg.max_cost_usd = 1.0;
    gw.configure(cfg);

    REQUIRE(gw.is_within_cost_limit());
    gw.record_cost(0.5);
    REQUIRE(gw.is_within_cost_limit());
    gw.record_cost(0.6);
    REQUIRE_FALSE(gw.is_within_cost_limit());
}

TEST_CASE("InferenceGateway cost limit zero means unlimited", "[gateway]") {
    InferenceGateway gw;
    InferenceGatewayConfig cfg;
    cfg.enabled = true;
    cfg.max_cost_usd = 0.0;
    gw.configure(cfg);

    gw.record_cost(999.99);
    REQUIRE(gw.is_within_cost_limit());
}

TEST_CASE("InferenceGateway to_json", "[gateway]") {
    InferenceGateway gw;
    InferenceGatewayConfig cfg;
    cfg.enabled = true;
    cfg.allowed_models = {"gpt-4"};
    cfg.max_cost_usd = 5.0;
    gw.configure(cfg);
    gw.record_cost(1.23);

    auto j = gw.to_json();
    REQUIRE(j["enabled"] == true);
    REQUIRE(j["max_cost_usd"] == 5.0);
    REQUIRE(j["current_cost_usd"] == 1.23);
}
