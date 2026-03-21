#include <catch2/catch_test_macros.hpp>
#include <clove/privacy_filter.hpp>

using namespace clove;

static PrivacyFilter make_filter() {
    PrivacyFilter pf;
    PrivacyFilterConfig cfg;
    cfg.enabled = true;
    cfg.mode = PrivacyMode::REDACT;
    pf.configure(cfg);
    return pf;
}

TEST_CASE("PrivacyFilter detects SSN", "[pii]") {
    auto pf = make_filter();
    auto matches = pf.scan("My SSN is 123-45-6789");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].type == PiiType::SSN);
    REQUIRE(matches[0].matched_text == "123-45-6789");
}

TEST_CASE("PrivacyFilter detects email", "[pii]") {
    auto pf = make_filter();
    auto matches = pf.scan("Contact alice@example.com for info");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].type == PiiType::EMAIL);
}

TEST_CASE("PrivacyFilter detects phone number", "[pii]") {
    auto pf = make_filter();
    auto matches = pf.scan("Call 555-123-4567 today");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].type == PiiType::PHONE);
}

TEST_CASE("PrivacyFilter detects credit card", "[pii]") {
    auto pf = make_filter();
    auto matches = pf.scan("Card: 4111-1111-1111-1111");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].type == PiiType::CREDIT_CARD);
}

TEST_CASE("PrivacyFilter detects IP address", "[pii]") {
    auto pf = make_filter();
    auto matches = pf.scan("Server at 192.168.1.100");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].type == PiiType::IP_ADDRESS);
}

TEST_CASE("PrivacyFilter detects multiple PII types", "[pii]") {
    auto pf = make_filter();
    auto matches = pf.scan("SSN: 123-45-6789, email: bob@test.com, IP: 10.0.0.1");
    REQUIRE(matches.size() == 3);
}

TEST_CASE("PrivacyFilter redact replaces PII", "[pii]") {
    auto pf = make_filter();
    auto result = pf.redact("My SSN is 123-45-6789 and email is test@x.com");
    REQUIRE(result.cleaned_text.find("123-45-6789") == std::string::npos);
    REQUIRE(result.cleaned_text.find("test@x.com") == std::string::npos);
    REQUIRE(result.cleaned_text.find("[REDACTED:") != std::string::npos);
    REQUIRE(result.matches.size() == 2);
}

TEST_CASE("PrivacyFilter contains_pii fast check", "[pii]") {
    auto pf = make_filter();
    REQUIRE(pf.contains_pii("Call 555-123-4567"));
    REQUIRE_FALSE(pf.contains_pii("No PII here, just normal text."));
}

TEST_CASE("PrivacyFilter disabled returns no matches", "[pii]") {
    PrivacyFilter pf;
    // Not configured — disabled by default
    auto matches = pf.scan("SSN: 123-45-6789");
    REQUIRE(matches.empty());
}

TEST_CASE("PrivacyFilter mode_from_string", "[pii]") {
    REQUIRE(PrivacyFilter::mode_from_string("audit") == PrivacyMode::AUDIT);
    REQUIRE(PrivacyFilter::mode_from_string("redact") == PrivacyMode::REDACT);
    REQUIRE(PrivacyFilter::mode_from_string("block") == PrivacyMode::BLOCK);
    REQUIRE(PrivacyFilter::mode_from_string("unknown") == PrivacyMode::AUDIT);
}
