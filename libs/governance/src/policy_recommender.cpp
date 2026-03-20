#include <clove/policy_recommender.hpp>

#include <algorithm>

namespace clove {

// ---------------------------------------------------------------------------
// Record denial
// ---------------------------------------------------------------------------

void PolicyRecommender::record_denial(const DenialEvent& event) {
    std::lock_guard lock(mutex_);

    std::string key = event.category + ":" + event.resource;

    auto it = aggregates_.find(key);
    if (it == aggregates_.end()) {
        aggregates_[key] = DenialAggregate{
            .category      = event.category,
            .resource      = event.resource,
            .count         = 1,
            .first_seen_ms = event.timestamp_ms,
            .last_seen_ms  = event.timestamp_ms,
        };
    } else {
        it->second.count++;
        if (event.timestamp_ms < it->second.first_seen_ms) {
            it->second.first_seen_ms = event.timestamp_ms;
        }
        if (event.timestamp_ms > it->second.last_seen_ms) {
            it->second.last_seen_ms = event.timestamp_ms;
        }
    }
}

// ---------------------------------------------------------------------------
// Generate recommendations
// ---------------------------------------------------------------------------

std::vector<PolicyRecommendation> PolicyRecommender::get_recommendations(
    size_t max) const {
    std::lock_guard lock(mutex_);

    // Collect all aggregates into a sortable vector
    std::vector<const DenialAggregate*> sorted;
    sorted.reserve(aggregates_.size());
    for (const auto& [key, agg] : aggregates_) {
        sorted.push_back(&agg);
    }

    // Sort by count descending
    std::sort(sorted.begin(), sorted.end(),
              [](const DenialAggregate* a, const DenialAggregate* b) {
                  return a->count > b->count;
              });

    // Truncate
    if (sorted.size() > max) {
        sorted.resize(max);
    }

    // Build recommendations
    std::vector<PolicyRecommendation> recs;
    recs.reserve(sorted.size());

    for (const auto* agg : sorted) {
        PolicyRecommendation rec;
        rec.category         = agg->category;
        rec.resource         = agg->resource;
        rec.occurrence_count = agg->count;

        if (agg->category == "http") {
            rec.action = "add_domain";
            rec.suggested_change = nlohmann::json{
                {"action", "add_domain"},
                {"domain", agg->resource},
            };
        } else if (agg->category == "exec") {
            rec.action = "add_command";
            rec.suggested_change = nlohmann::json{
                {"action", "add_command"},
                {"command", agg->resource},
            };
        } else if (agg->category == "read") {
            rec.action = "add_read_path";
            rec.suggested_change = nlohmann::json{
                {"action", "add_read_path"},
                {"path", agg->resource},
            };
        } else if (agg->category == "write") {
            rec.action = "add_write_path";
            rec.suggested_change = nlohmann::json{
                {"action", "add_write_path"},
                {"path", agg->resource},
            };
        } else if (agg->category == "llm") {
            rec.action = "increase_quota";
            rec.suggested_change = nlohmann::json{
                {"action", "increase_quota"},
                {"resource", agg->resource},
            };
        } else {
            rec.action = "allow_resource";
            rec.suggested_change = nlohmann::json{
                {"action", "allow_resource"},
                {"category", agg->category},
                {"resource", agg->resource},
            };
        }

        recs.push_back(std::move(rec));
    }

    return recs;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

void PolicyRecommender::clear() {
    std::lock_guard lock(mutex_);
    aggregates_.clear();
}

size_t PolicyRecommender::denial_count() const {
    std::lock_guard lock(mutex_);

    size_t total = 0;
    for (const auto& [key, agg] : aggregates_) {
        total += agg.count;
    }
    return total;
}

} // namespace clove
