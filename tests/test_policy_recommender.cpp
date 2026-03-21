#include <catch2/catch_test_macros.hpp>
#include <clove/policy_recommender.hpp>

using namespace clove;

TEST_CASE("PolicyRecommender starts empty", "[policy]") {
    PolicyRecommender pr;
    REQUIRE(pr.denial_count() == 0);
    REQUIRE(pr.get_recommendations().empty());
}

TEST_CASE("PolicyRecommender records denials", "[policy]") {
    PolicyRecommender pr;
    pr.record_denial({1, "http", "api.example.com", "domain blocked", 1000});
    pr.record_denial({1, "http", "api.example.com", "domain blocked", 2000});
    pr.record_denial({2, "exec", "rm", "command blocked", 3000});

    REQUIRE(pr.denial_count() == 3);
}

TEST_CASE("PolicyRecommender generates recommendations", "[policy]") {
    PolicyRecommender pr;
    // Same denial 5 times
    for (int i = 0; i < 5; i++) {
        pr.record_denial({1, "http", "api.example.com", "blocked", static_cast<uint64_t>(i * 1000)});
    }

    auto recs = pr.get_recommendations();
    REQUIRE(recs.size() >= 1);
    REQUIRE(recs[0].category == "http");
    REQUIRE(recs[0].resource == "api.example.com");
    REQUIRE(recs[0].occurrence_count == 5);
}

TEST_CASE("PolicyRecommender clear", "[policy]") {
    PolicyRecommender pr;
    pr.record_denial({1, "http", "x.com", "blocked", 0});
    pr.clear();
    REQUIRE(pr.denial_count() == 0);
    REQUIRE(pr.get_recommendations().empty());
}

TEST_CASE("PolicyRecommender max limit", "[policy]") {
    PolicyRecommender pr;
    for (int i = 0; i < 50; i++) {
        pr.record_denial({1, "http", "domain" + std::to_string(i) + ".com", "blocked", 0});
    }

    auto recs = pr.get_recommendations(5);
    REQUIRE(recs.size() <= 5);
}
