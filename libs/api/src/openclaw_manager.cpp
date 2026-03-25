/**
 * OpenClaw Manager — spawn and manage OpenClaw instances inside CLOVE sandboxes.
 *
 * Each instance gets:
 *   1. Its own temp config dir with SOUL.md + openclaw.json
 *   2. openclaw.json points LLM at CLOVE's /api/v1/chat/completions (cost tracked, PII filtered)
 *   3. A CLOVE Sandbox (Seatbelt on macOS, namespaces+seccomp on Linux)
 *   4. Budget and permission enforcement via the kernel
 */

#include <clove/openclaw_manager.hpp>
#include <clove/config.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <random>
#include <chrono>
#include <cstdlib>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace clove {

OpenClawManager::OpenClawManager(
    SandboxManager& sandbox_mgr,
    AuditLogger& audit,
    const KernelConfig& config)
    : sandbox_mgr_(sandbox_mgr)
    , audit_(audit)
    , config_(config)
{}

std::string OpenClawManager::find_openclaw_binary() const {
    // Check common locations
    const std::vector<std::string> paths = {
        // Global install
        "/usr/local/bin/openclaw",
        "/opt/homebrew/bin/openclaw",
        // NVM / global npm
        std::string(getenv("HOME") ? getenv("HOME") : "") + "/.nvm/versions/node/v24.0.0/bin/openclaw",
        // NemoClaw's bundled copy
        std::string(getenv("HOME") ? getenv("HOME") : "") + "/Documents/NemoClaw/node_modules/.bin/openclaw",
    };

    // Check env var first
    const char* env = getenv("OPENCLAW_PATH");
    if (env && fs::exists(env)) return env;

    // Check PATH via which
    FILE* pipe = popen("which openclaw 2>/dev/null", "r");
    if (pipe) {
        char buf[512];
        if (fgets(buf, sizeof(buf), pipe)) {
            std::string result(buf);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            pclose(pipe);
            if (!result.empty() && fs::exists(result)) return result;
        } else {
            pclose(pipe);
        }
    }

    for (const auto& p : paths) {
        if (!p.empty() && fs::exists(p)) return p;
    }

    return "";
}

std::string OpenClawManager::generate_id() const {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0x1000, 0xFFFF);
    char buf[32];
    snprintf(buf, sizeof(buf), "oc_%llx_%04x", static_cast<unsigned long long>(ms), dis(gen));
    return std::string(buf);
}

std::string OpenClawManager::create_config_dir(const OpenClawInstanceConfig& cfg, int api_port) const {
    std::string dir = "/tmp/clove-openclaw-" + cfg.name + "-" + std::to_string(api_port);
    fs::create_directories(dir);

    // Write SOUL.md
    if (!cfg.soul.empty()) {
        std::ofstream soul(dir + "/SOUL.md");
        soul << cfg.soul;
    }

    // Write openclaw.json — routes LLM through CLOVE kernel
    json oc_config;

    // Point LLM at CLOVE's OpenAI-compatible endpoint
    oc_config["models"] = json{
        {"default", json{
            {"provider", "openai-compatible"},
            {"baseUrl", "http://localhost:" + std::to_string(config_.api_port) + "/api/v1"},
            {"apiKey", "clove-internal"},
        }}
    };

    // Set the model
    if (!cfg.model.empty()) {
        oc_config["models"]["default"]["model"] = cfg.model;
    }

    // Gateway config
    oc_config["gateway"] = json{
        {"port", api_port},
        {"headless", true},
    };

    // Sandbox — disable OpenClaw's own Docker sandbox, CLOVE handles it
    oc_config["agents"] = json{
        {"defaults", json{
            {"sandbox", json{{"mode", "off"}}},
        }}
    };

    // Channels
    // Note: channel credentials come from the user's ~/.openclaw/ dir,
    // we just ensure the config allows them
    if (!cfg.channels.empty()) {
        json channels = json::object();
        for (const auto& ch : cfg.channels) {
            channels[ch] = json{{"enabled", true}};
        }
        oc_config["channels"] = channels;
    }

    // Skills
    if (!cfg.skills.empty()) {
        json skills = json::array();
        for (const auto& s : cfg.skills) {
            skills.push_back(s);
        }
        oc_config["skills"] = json{{"enabled", skills}};
    }

    std::ofstream config_file(dir + "/openclaw.json");
    config_file << oc_config.dump(2);

    spdlog::debug("Created OpenClaw config dir: {}", dir);
    return dir;
}

std::string OpenClawManager::spawn(const OpenClawInstanceConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);

    // Find OpenClaw binary
    std::string binary = find_openclaw_binary();
    if (binary.empty()) {
        spdlog::error("OpenClaw binary not found. Install with: npm install -g @openclaw/cli");
        audit_.log(AuditCategory::SECURITY, "OPENCLAW_SPAWN_FAILED", 0, cfg.name,
            {{"reason", "binary not found"}}, false);
        return "";
    }

    // Generate ID and allocate port
    std::string id = generate_id();
    int port = next_port_++;

    // Create config directory
    std::string config_dir = create_config_dir(cfg, port);

    // Create sandbox config
    SandboxConfig sbox_cfg;
    sbox_cfg.name = "openclaw-" + cfg.name;
    sbox_cfg.enable_network = true;  // OpenClaw needs network for chat platforms

    // macOS: enable Seatbelt; Linux: enable namespaces + seccomp
    sbox_cfg.enable_landlock = true;
    sbox_cfg.enable_seccomp = true;

    // File access
    sbox_cfg.allowed_paths = cfg.allowed_read_paths;
    sbox_cfg.writable_paths = cfg.allowed_write_paths;

    // Always allow reading the config dir and OpenClaw's own dirs
    const char* home = getenv("HOME");
    if (home) {
        sbox_cfg.allowed_paths.push_back(std::string(home) + "/.openclaw");
        sbox_cfg.allowed_paths.push_back(std::string(home) + "/.nvm");
        sbox_cfg.allowed_paths.push_back(std::string(home) + "/.npm");
        sbox_cfg.allowed_paths.push_back(std::string(home) + "/.config");
        sbox_cfg.writable_paths.push_back(std::string(home) + "/.openclaw");
    }
    sbox_cfg.allowed_paths.push_back(config_dir);
    sbox_cfg.writable_paths.push_back(config_dir);

    // Resource limits
    sbox_cfg.limits.memory_limit_bytes = static_cast<uint64_t>(cfg.sandbox_memory_mb) * 1024 * 1024;
    sbox_cfg.limits.cpu_quota_us = cfg.sandbox_cpu_percent * 10000;  // percent → microseconds per 1s
    sbox_cfg.limits.cpu_period_us = 1000000;
    sbox_cfg.limits.max_pids = 64;

    // Create and start sandbox
    auto sandbox = sandbox_mgr_.create_sandbox(sbox_cfg);
    if (!sandbox) {
        spdlog::error("Failed to create sandbox for OpenClaw instance {}", cfg.name);
        return "";
    }

    // Build OpenClaw command args
    std::vector<std::string> args = {
        "gateway",
        "--headless",
        "--config", config_dir + "/openclaw.json",
    };

    // If SOUL.md exists, set the profile dir
    if (!cfg.soul.empty()) {
        args.push_back("--profile");
        args.push_back(config_dir);
    }

    spdlog::info("Spawning OpenClaw instance '{}' (id={}, port={})", cfg.name, id, port);

    if (!sandbox->start(binary, args)) {
        spdlog::error("Failed to start OpenClaw instance {}", cfg.name);
        sandbox_mgr_.remove_sandbox(sbox_cfg.name);
        return "";
    }

    // Record instance
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    OpenClawInstance instance;
    instance.id = id;
    instance.name = cfg.name;
    instance.config = cfg;
    instance.sandbox = sandbox;
    instance.config_dir = config_dir;
    instance.api_port = port;
    instance.state = "running";
    instance.started_at_ms = ms;

    instances_[id] = std::move(instance);

    // Audit
    audit_.log(AuditCategory::RESOURCE, "OPENCLAW_SPAWNED", 0, cfg.name, {
        {"instance_id", id},
        {"port", port},
        {"budget_usd", cfg.budget_usd},
        {"channels", cfg.channels},
        {"sandbox", sbox_cfg.name},
        {"pid", sandbox->pid()},
    });

    spdlog::info("OpenClaw '{}' running (PID={}, port={}, budget=${})",
        cfg.name, sandbox->pid(), port, cfg.budget_usd);

    return id;
}

std::vector<std::string> OpenClawManager::spawn_fleet(const std::vector<OpenClawInstanceConfig>& configs) {
    std::vector<std::string> ids;
    ids.reserve(configs.size());
    for (const auto& cfg : configs) {
        std::string id = spawn(cfg);
        ids.push_back(id);
    }
    return ids;
}

bool OpenClawManager::stop(const std::string& id) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = instances_.find(id);
    if (it == instances_.end()) return false;

    auto& inst = it->second;
    spdlog::info("Stopping OpenClaw instance '{}' (id={})", inst.name, id);

    if (inst.sandbox && inst.sandbox->is_running()) {
        inst.sandbox->stop();
    }
    inst.state = "stopped";

    // Cleanup config dir
    try {
        fs::remove_all(inst.config_dir);
    } catch (...) {}

    // Cleanup sandbox
    sandbox_mgr_.remove_sandbox("openclaw-" + inst.name);

    audit_.log(AuditCategory::RESOURCE, "OPENCLAW_STOPPED", 0, inst.name, {
        {"instance_id", id},
    });

    instances_.erase(it);
    return true;
}

void OpenClawManager::stop_all() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& [id, _] : instances_) {
            ids.push_back(id);
        }
    }
    for (const auto& id : ids) {
        stop(id);
    }
}

const OpenClawInstance* OpenClawManager::get(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = instances_.find(id);
    if (it != instances_.end()) {
        // Check if still running
        if (it->second.sandbox && !it->second.sandbox->is_running()) {
            const_cast<OpenClawInstance&>(it->second).state = "stopped";
        }
        return &it->second;
    }
    return nullptr;
}

std::vector<const OpenClawInstance*> OpenClawManager::list() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<const OpenClawInstance*> result;
    result.reserve(instances_.size());
    for (const auto& [_, inst] : instances_) {
        result.push_back(&inst);
    }
    return result;
}

size_t OpenClawManager::count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return instances_.size();
}

} // namespace clove
