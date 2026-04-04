#include <clove/memory_manager.hpp>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace clove {

MemoryManager::MemoryManager(Database& db) : store_(db) {}

// ── remember ──────────────────────────────────────────────────────────────────

std::string MemoryManager::remember(
    const std::string& agent_name,
    const std::string& workspace_id,
    const std::string& run_id,
    int current_step,
    const std::string& fact)
{
    if (fact.empty()) return "(empty fact, not stored)";

    MemoryEntry e;
    e.id           = generate_id();
    e.agent_name   = agent_name;
    e.workspace_id = workspace_id;
    e.tier         = MemoryTier::SEMANTIC;  // explicit remember → straight to SEMANTIC
    e.content      = fact.substr(0, 1000);
    e.importance   = score_importance(fact);
    e.recency_step = current_step;
    e.source_run_id = run_id;
    e.created_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (!store_.write(e)) return "(store write failed)";
    return "remembered: " + fact.substr(0, 80);
}

// ── recall ────────────────────────────────────────────────────────────────────

std::string MemoryManager::recall(
    const std::string& agent_name,
    const std::string& /*workspace_id*/,
    const std::string& query,
    int current_step,
    size_t token_budget)
{
    auto entries = store_.retrieve(agent_name, query, current_step, token_budget);
    if (entries.empty()) return "(no memories found)";

    std::string result;
    for (const auto& e : entries) {
        std::string label;
        switch (e.tier) {
            case MemoryTier::PROCEDURAL: label = "PATTERN"; break;
            case MemoryTier::SEMANTIC:   label = "MEMORY";  break;
            case MemoryTier::EPISODIC:   label = "RECENT";  break;
        }
        result += "[" + label + " | score:" +
                  std::to_string(static_cast<int>(e.score * 100)) + "%] " +
                  e.content + "\n\n";
    }
    return result;
}

// ── consolidate ───────────────────────────────────────────────────────────────

void MemoryManager::consolidate(const std::string& agent_name, const std::string& run_id) {
    if (agent_name.empty() || run_id.empty()) return;
    store_.consolidate(agent_name, run_id);
}

// ── abstract (daemon call) ────────────────────────────────────────────────────

void MemoryManager::abstract(const std::string& agent_name) {
    if (agent_name.empty()) return;
    store_.abstract_to_procedural(agent_name);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

float MemoryManager::score_importance(const std::string& content) const {
    std::string low = content;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    if (low.find("critical")  != std::string::npos ||
        low.find("error")     != std::string::npos ||
        low.find("bug")       != std::string::npos ||
        low.find("fail")      != std::string::npos ||
        low.find("important") != std::string::npos) return 8.5f;

    if (low.find("found")     != std::string::npos ||
        low.find("created")   != std::string::npos ||
        low.find("completed") != std::string::npos ||
        low.find("fixed")     != std::string::npos ||
        low.find("result")    != std::string::npos) return 7.5f;

    if (low.find("note")      != std::string::npos ||
        low.find("key")       != std::string::npos ||
        low.find("remember")  != std::string::npos) return 7.0f;

    return 5.5f;
}

std::string MemoryManager::generate_id() const {
    auto ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::ostringstream ss;
    ss << "mem-" << std::hex << ns;
    return ss.str();
}

} // namespace clove
