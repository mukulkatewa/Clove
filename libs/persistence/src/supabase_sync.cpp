#include <clove/supabase_sync.hpp>
#include <spdlog/spdlog.h>

// Use the same httplib that the API server uses
#include <httplib.h>

namespace clove {

SupabaseSync::SupabaseSync(Config cfg) : cfg_(std::move(cfg)) {
    if (!is_configured()) return;
    worker_ = std::thread([this] { run(); });
    spdlog::info("SupabaseSync: background sync to {} started", cfg_.project_url);
}

SupabaseSync::~SupabaseSync() {
    flush();
    running_ = false;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void SupabaseSync::upsert(const std::string& table, const nlohmann::json& row) {
    if (!is_configured()) return;
    std::lock_guard lock(mu_);
    queue_.push({Op::Kind::Upsert, table, "", "", row});
    cv_.notify_one();
}

void SupabaseSync::remove(const std::string& table,
                           const std::string& pk_col, const std::string& pk_val) {
    if (!is_configured()) return;
    std::lock_guard lock(mu_);
    queue_.push({Op::Kind::Delete, table, pk_col, pk_val, {}});
    cv_.notify_one();
}

void SupabaseSync::patch(const std::string& table, const std::string& pk_col,
                          const std::string& pk_val, const nlohmann::json& fields) {
    if (!is_configured()) return;
    std::lock_guard lock(mu_);
    queue_.push({Op::Kind::Patch, table, pk_col, pk_val, fields});
    cv_.notify_one();
}

void SupabaseSync::flush() {
    if (!is_configured()) return;
    // Drain the queue in the calling thread
    std::unique_lock lock(mu_);
    while (!queue_.empty()) {
        Op op = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        switch (op.kind) {
            case Op::Kind::Upsert: http_upsert(op); break;
            case Op::Kind::Delete: http_delete(op); break;
            case Op::Kind::Patch:  http_patch(op);  break;
        }
        lock.lock();
    }
}

size_t SupabaseSync::queue_depth() const {
    std::lock_guard lock(mu_);
    return queue_.size();
}

void SupabaseSync::run() {
    while (running_) {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
        if (!running_ && queue_.empty()) break;

        Op op = std::move(queue_.front());
        queue_.pop();
        lock.unlock();

        bool ok = false;
        switch (op.kind) {
            case Op::Kind::Upsert: ok = http_upsert(op); break;
            case Op::Kind::Delete: ok = http_delete(op); break;
            case Op::Kind::Patch:  ok = http_patch(op);  break;
        }
        if (!ok) {
            spdlog::warn("SupabaseSync: failed to sync {} op on table '{}'",
                op.kind == Op::Kind::Upsert ? "upsert" :
                op.kind == Op::Kind::Delete ? "delete" : "patch",
                op.table);
        }
    }
}

std::string SupabaseSync::rest_url(const std::string& table) const {
    return "/rest/v1/" + table;
}

// Strip the https:// prefix to get the host for httplib
static std::pair<std::string,int> parse_host(const std::string& url) {
    std::string host = url;
    if (host.rfind("https://", 0) == 0) host = host.substr(8);
    if (host.rfind("http://",  0) == 0) host = host.substr(7);
    // Remove trailing slash
    if (!host.empty() && host.back() == '/') host.pop_back();
    return {host, 443};
}

bool SupabaseSync::http_upsert(const Op& op) {
    try {
        auto [host, port] = parse_host(cfg_.project_url);
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.enable_server_certificate_verification(false);

        httplib::Headers headers = {
            {"apikey",        cfg_.service_key},
            {"Authorization", "Bearer " + cfg_.service_key},
            {"Content-Type",  "application/json"},
            {"Prefer",        "resolution=merge-duplicates,return=minimal"}
        };

        auto res = cli.Post(rest_url(op.table), headers,
                            op.payload.dump(), "application/json");
        return res && (res->status == 200 || res->status == 201 || res->status == 204);
    } catch (const std::exception& e) {
        spdlog::warn("SupabaseSync::http_upsert exception: {}", e.what());
        return false;
    }
}

bool SupabaseSync::http_delete(const Op& op) {
    try {
        auto [host, port] = parse_host(cfg_.project_url);
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.enable_server_certificate_verification(false);

        httplib::Headers headers = {
            {"apikey",        cfg_.service_key},
            {"Authorization", "Bearer " + cfg_.service_key}
        };

        std::string path = rest_url(op.table) + "?" + op.pk_col + "=eq." + op.pk_val;
        auto res = cli.Delete(path, headers);
        return res && (res->status == 200 || res->status == 204);
    } catch (const std::exception& e) {
        spdlog::warn("SupabaseSync::http_delete exception: {}", e.what());
        return false;
    }
}

bool SupabaseSync::http_patch(const Op& op) {
    try {
        auto [host, port] = parse_host(cfg_.project_url);
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.enable_server_certificate_verification(false);

        httplib::Headers headers = {
            {"apikey",        cfg_.service_key},
            {"Authorization", "Bearer " + cfg_.service_key},
            {"Content-Type",  "application/json"},
            {"Prefer",        "return=minimal"}
        };

        std::string path = rest_url(op.table) + "?" + op.pk_col + "=eq." + op.pk_val;
        auto res = cli.Patch(path, headers,
                             op.payload.dump(), "application/json");
        return res && (res->status == 200 || res->status == 204);
    } catch (const std::exception& e) {
        spdlog::warn("SupabaseSync::http_patch exception: {}", e.what());
        return false;
    }
}

} // namespace clove
