#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace clove {

enum class ArtifactState : uint8_t {
    DRAFT      = 0,
    IN_REVIEW  = 1,
    APPROVED   = 2,
    FINAL      = 3,
    ARCHIVED   = 4,
};

enum class ArtifactType : uint8_t {
    QUERY     = 0,
    RESEARCH  = 1,
    ANALYSIS  = 2,
    SYNTHESIS = 3,
    REPORT    = 4,
    NOTE      = 5,
    PLAN      = 6,
};

struct Artifact {
    std::string id;
    std::string chain_id;
    uint32_t author_agent_id = 0;
    ArtifactType type = ArtifactType::NOTE;
    ArtifactState state = ArtifactState::DRAFT;
    std::string title;
    std::string content;
    std::vector<std::string> parent_ids;
    nlohmann::json metadata = nlohmann::json::object();
    uint64_t created_at_ms = 0;
    uint64_t updated_at_ms = 0;
};

struct Chain {
    std::string id;
    std::string name;
    std::string description;
    uint32_t creator_agent_id = 0;
    std::vector<std::string> artifact_ids;
    nlohmann::json metadata = nlohmann::json::object();
    uint64_t created_at_ms = 0;
};

// --- ID generation ---

inline std::string generate_hex_id(size_t len = 12) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len);
    std::uniform_int_distribution<int> dist(0, 15);
    for (size_t i = 0; i < len; ++i) out += hex[dist(rng)];
    return out;
}

inline std::string generate_artifact_id() { return "art_" + generate_hex_id(); }
inline std::string generate_chain_id()    { return "chain_" + generate_hex_id(); }

inline uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// --- String conversion ---

inline const char* artifact_state_to_string(ArtifactState s) {
    switch (s) {
        case ArtifactState::DRAFT:     return "draft";
        case ArtifactState::IN_REVIEW: return "in_review";
        case ArtifactState::APPROVED:  return "approved";
        case ArtifactState::FINAL:     return "final";
        case ArtifactState::ARCHIVED:  return "archived";
    }
    return "unknown";
}

inline ArtifactState artifact_state_from_string(const std::string& s) {
    if (s == "draft")     return ArtifactState::DRAFT;
    if (s == "in_review") return ArtifactState::IN_REVIEW;
    if (s == "approved")  return ArtifactState::APPROVED;
    if (s == "final")     return ArtifactState::FINAL;
    if (s == "archived")  return ArtifactState::ARCHIVED;
    return ArtifactState::DRAFT;
}

inline const char* artifact_type_to_string(ArtifactType t) {
    switch (t) {
        case ArtifactType::QUERY:     return "query";
        case ArtifactType::RESEARCH:  return "research";
        case ArtifactType::ANALYSIS:  return "analysis";
        case ArtifactType::SYNTHESIS: return "synthesis";
        case ArtifactType::REPORT:    return "report";
        case ArtifactType::NOTE:      return "note";
        case ArtifactType::PLAN:      return "plan";
    }
    return "unknown";
}

inline ArtifactType artifact_type_from_string(const std::string& s) {
    if (s == "query")     return ArtifactType::QUERY;
    if (s == "research")  return ArtifactType::RESEARCH;
    if (s == "analysis")  return ArtifactType::ANALYSIS;
    if (s == "synthesis") return ArtifactType::SYNTHESIS;
    if (s == "report")    return ArtifactType::REPORT;
    if (s == "note")      return ArtifactType::NOTE;
    if (s == "plan")      return ArtifactType::PLAN;
    return ArtifactType::NOTE;
}

// --- JSON serialization ---

inline nlohmann::json artifact_to_json(const Artifact& a) {
    return {
        {"id", a.id},
        {"chain_id", a.chain_id},
        {"author_agent_id", a.author_agent_id},
        {"type", artifact_type_to_string(a.type)},
        {"state", artifact_state_to_string(a.state)},
        {"title", a.title},
        {"content", a.content},
        {"parent_ids", a.parent_ids},
        {"metadata", a.metadata},
        {"created_at_ms", a.created_at_ms},
        {"updated_at_ms", a.updated_at_ms},
    };
}

inline nlohmann::json chain_to_json(const Chain& c) {
    return {
        {"id", c.id},
        {"name", c.name},
        {"description", c.description},
        {"creator_agent_id", c.creator_agent_id},
        {"artifact_ids", c.artifact_ids},
        {"metadata", c.metadata},
        {"created_at_ms", c.created_at_ms},
    };
}

} // namespace clove
