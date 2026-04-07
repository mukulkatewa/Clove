#include "kernel.hpp"
#include <clove/version.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <curl/curl.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace term {
    constexpr const char* RESET  = "\033[0m";
    constexpr const char* BOLD   = "\033[1m";
    constexpr const char* DIM    = "\033[2m";
    constexpr const char* CYAN   = "\033[36m";
    constexpr const char* GREEN  = "\033[32m";
    constexpr const char* YELLOW = "\033[33m";
}

void print_banner() {
    std::cout << term::CYAN << term::BOLD << R"(
    ╔═══════════════════════════════════════════╗
    ║   ██████╗██╗      ██████╗ ██╗   ██╗███████╗  ║
    ║  ██╔════╝██║     ██╔═══██╗██║   ██║██╔════╝  ║
    ║  ██║     ██║     ██║   ██║██║   ██║█████╗    ║
    ║  ██║     ██║     ██║   ██║╚██╗ ██╔╝██╔══╝    ║
    ║  ╚██████╗███████╗╚██████╔╝ ╚████╔╝ ███████╗  ║
    ║   ╚═════╝╚══════╝ ╚═════╝   ╚═══╝  ╚══════╝  ║
    ║)" << term::RESET << term::DIM << "  Agent Fleet Infrastructure v2" << term::CYAN << term::BOLD << R"(         ║
    ╚═══════════════════════════════════════════╝
)" << term::RESET;
}

int main(int argc, char** argv) {
    // Must be called once before any curl_easy_init() across all threads
    curl_global_init(CURL_GLOBAL_ALL);

    print_banner();

    // Load .env file if present (check current dir, then parent)
    auto load_env = [](const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Don't overwrite existing env vars
            setenv(key.c_str(), val.c_str(), 0);
        }
    };
    load_env(".env");
    load_env("examples/.env");

    clove::KernelConfig config;

    // Auto-configure from environment variables
    const char* or_key = getenv("OPENROUTER_API_KEY");
    if (or_key && or_key[0]) {
        config.openrouter_enabled = true;
        config.openrouter_api_key = or_key;
    }
    const char* api_key_env = getenv("CLOVE_API_KEY");
    if (api_key_env && api_key_env[0]) {
        config.api_key = api_key_env;
    }
    // Railway (and similar PaaS) set PORT at runtime — use it if present
    const char* port_env = getenv("PORT");
    if (port_env && port_env[0]) {
        config.api_port = static_cast<uint16_t>(std::stoi(port_env));
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-sandbox")       config.enable_sandboxing = false;
        else if (arg == "--socket" && i+1 < argc)  config.socket_path = argv[++i];
        // Inference gateway
        else if (arg == "--llm-proxy")   config.llm_proxy_enabled = true;
        else if (arg == "--llm-allowed-providers" && i+1 < argc) {
            std::string s = argv[++i];
            size_t pos;
            while ((pos = s.find(',')) != std::string::npos) {
                config.llm_allowed_providers.push_back(s.substr(0, pos));
                s.erase(0, pos + 1);
            }
            if (!s.empty()) config.llm_allowed_providers.push_back(s);
        }
        else if (arg == "--llm-allowed-models" && i+1 < argc) {
            std::string s = argv[++i];
            size_t pos;
            while ((pos = s.find(',')) != std::string::npos) {
                config.llm_allowed_models.push_back(s.substr(0, pos));
                s.erase(0, pos + 1);
            }
            if (!s.empty()) config.llm_allowed_models.push_back(s);
        }
        else if (arg == "--llm-max-cost" && i+1 < argc) config.llm_max_cost_usd = std::stod(argv[++i]);
        // Privacy
        else if (arg == "--privacy")     config.privacy_enabled = true;
        else if (arg == "--privacy-mode" && i+1 < argc) config.privacy_mode = argv[++i];
        // Egress
        else if (arg == "--egress-proxy") config.egress_proxy_enabled = true;
        else if (arg == "--egress-port" && i+1 < argc) config.egress_proxy_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        // Policy / manifest
        else if (arg == "--policy-file" && i+1 < argc) config.policy_file = argv[++i];
        else if (arg == "--manifest" && i+1 < argc)    config.manifest_file = argv[++i];
        // Database
        else if (arg == "--db" && i+1 < argc) config.db_path = argv[++i];
        // API
        else if (arg == "--api")         config.api_enabled = true;
        else if (arg == "--no-api")      config.api_enabled = false;
        else if (arg == "--api-port" && i+1 < argc) config.api_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--api-key" && i+1 < argc)  config.api_key = argv[++i];
        // OpenRouter
        else if (arg == "--openrouter")  config.openrouter_enabled = true;
        else if (arg == "--openrouter-key" && i+1 < argc) config.openrouter_api_key = argv[++i];
        else if (arg == "--llm-model" && i+1 < argc) config.llm_model = argv[++i];
        // Integrations
        else if (arg == "--otel")        config.otel_enabled = true;
        else if (arg == "--otel-endpoint" && i+1 < argc) config.otel_endpoint = argv[++i];
        else if (arg == "--mcp")         config.mcp_enabled = true;
        else if (arg == "--a2a")         config.a2a_enabled = true;
        // Legacy positional
        else if (arg[0] != '-')          config.socket_path = arg;
    }

#ifdef __APPLE__
    if (config.enable_sandboxing) {
        std::cout << "    " << term::GREEN << "\u2713" << term::RESET
                  << "  macOS: Seatbelt sandbox enabled\n";
    }
#endif

    // Setup logging
    spdlog::set_pattern("    %^[%l]%$ %v");
    spdlog::set_level(spdlog::level::info);

    // Boot
    std::cout << "    " << term::GREEN << "✓" << term::RESET << "  Starting CLOVE v" << clove::VERSION << "\n";

    clove::Kernel kernel(config);

    if (!kernel.init()) {
        std::cout << "\n    " << term::BOLD << "\033[31m✗" << term::RESET
                  << "  Failed to initialize CLOVE\n\n";
        return 1;
    }

    std::cout << "\n" << term::GREEN << term::BOLD
              << "    ═══════════════════════════════════════\n"
              << "      CLOVE READY" << term::RESET << term::DIM
              << "  ·  Ctrl+C to shutdown\n"
              << term::GREEN << term::BOLD
              << "    ═══════════════════════════════════════\n"
              << term::RESET << "\n";

    kernel.run();

    std::cout << "\n    " << term::YELLOW << "⟳" << term::RESET
              << "  Shutdown complete.\n\n";
    curl_global_cleanup();
    return 0;
}
