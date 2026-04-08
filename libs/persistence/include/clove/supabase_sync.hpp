#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace clove {

/// Async Supabase sync — pushes upserts/deletes to Supabase PostgREST API
/// from a background thread. Non-blocking for the kernel hot path.
class SupabaseSync {
public:
    struct Config {
        std::string project_url;   // e.g. https://pzldqapdbiszeumueyzh.supabase.co
        std::string service_key;   // service_role JWT — bypasses RLS
    };

    explicit SupabaseSync(Config cfg);
    ~SupabaseSync();

    SupabaseSync(const SupabaseSync&) = delete;
    SupabaseSync& operator=(const SupabaseSync&) = delete;

    bool is_configured() const { return !cfg_.project_url.empty() && !cfg_.service_key.empty(); }

    // Queue an upsert (INSERT OR REPLACE) into a Supabase table.
    // Fire-and-forget — returns immediately.
    void upsert(const std::string& table, const nlohmann::json& row);

    // Queue a delete by primary key column + value.
    void remove(const std::string& table, const std::string& pk_col, const std::string& pk_val);

    // Queue a PATCH (partial update) by primary key.
    void patch(const std::string& table, const std::string& pk_col,
               const std::string& pk_val, const nlohmann::json& fields);

    // Flush all pending writes synchronously (used on shutdown).
    void flush();

    size_t queue_depth() const;

private:
    struct Op {
        enum class Kind { Upsert, Delete, Patch } kind;
        std::string table;
        std::string pk_col;
        std::string pk_val;
        nlohmann::json payload;
    };

    Config cfg_;
    std::queue<Op> queue_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> running_{true};

    void run();
    bool http_upsert(const Op& op);
    bool http_delete(const Op& op);
    bool http_patch(const Op& op);
    std::string rest_url(const std::string& table) const;
};

} // namespace clove
