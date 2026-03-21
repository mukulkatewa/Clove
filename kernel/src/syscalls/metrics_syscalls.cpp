#include "mod.hpp"
#include <clove/agent_manager.hpp>
#include <clove/state_store.hpp>
#include <clove/audit_log.hpp>
#include <clove/execution_log.hpp>
#include <clove/llm_queue.hpp>
#include <clove/inference_gateway.hpp>
#include <sys/resource.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <fstream>
#endif

namespace clove {

void MetricsSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_METRICS_SYSTEM — kernel process metrics
    router.register_handler(SyscallOp::SYS_METRICS_SYSTEM,
        [this](const Message& msg) -> Message {
            json response;
            try {
                response["success"] = true;
                response["pid"] = getpid();

                // RSS via mach task_info (macOS) or /proc/self/status (Linux)
#ifdef __APPLE__
                struct mach_task_basic_info info;
                mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
                if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                              (task_info_t)&info, &count) == KERN_SUCCESS) {
                    response["rss_bytes"] = static_cast<uint64_t>(info.resident_size);
                    response["virtual_bytes"] = static_cast<uint64_t>(info.virtual_size);
                }
#else
                // Linux fallback
                {
                    std::ifstream status("/proc/self/status");
                    std::string line;
                    while (std::getline(status, line)) {
                        if (line.rfind("VmRSS:", 0) == 0) {
                            response["rss_kb"] = std::stoul(line.substr(6));
                        } else if (line.rfind("VmSize:", 0) == 0) {
                            response["virtual_kb"] = std::stoul(line.substr(7));
                        }
                    }
                }
#endif

                // Resource usage
                struct rusage ru;
                if (getrusage(RUSAGE_SELF, &ru) == 0) {
                    response["user_time_us"] = static_cast<uint64_t>(
                        ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec);
                    response["system_time_us"] = static_cast<uint64_t>(
                        ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec);
                    response["max_rss_kb"] = static_cast<uint64_t>(ru.ru_maxrss / 1024);
                }

                // Subsystem stats
                response["state_store_size"] = ctx_.state_store.size();
                response["audit_entry_count"] = ctx_.audit_logger.entry_count();
                response["execution_log_entries"] = ctx_.execution_logger.entry_count();
                response["llm_total_requests"] = ctx_.llm_queue.total_requests();
                response["llm_total_completed"] = ctx_.llm_queue.total_completed();

                auto gw_config = ctx_.inference_gateway.get_config();
                response["llm_cost_usd"] = gw_config.current_cost_usd;

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_METRICS_SYSTEM, response.dump());
        });

    // SYS_METRICS_AGENT — metrics for a specific agent
    router.register_handler(SyscallOp::SYS_METRICS_AGENT,
        [this](const Message& msg) -> Message {
            json response;
            try {
                uint32_t target_id = msg.agent_id();
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    target_id = req.value("agent_id", msg.agent_id());
                }

                auto agent = ctx_.agent_manager.get_agent(target_id);
                if (!agent) {
                    response["success"] = false;
                    response["error"] = "agent not found";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_METRICS_AGENT, response.dump());
                }

                auto metrics = agent->get_metrics();
                response["success"] = true;
                response["agent_id"] = metrics.id;
                response["name"] = metrics.name;
                response["pid"] = metrics.pid;
                response["state"] = static_cast<uint8_t>(metrics.state);
                response["memory_bytes"] = metrics.memory_bytes;
                response["cpu_percent"] = metrics.cpu_percent;
                response["uptime_seconds"] = metrics.uptime_seconds;
                response["llm_request_count"] = metrics.llm_request_count;
                response["llm_tokens_used"] = metrics.llm_tokens_used;

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_METRICS_AGENT, response.dump());
        });

    // SYS_METRICS_ALL_AGENTS — metrics for all agents
    router.register_handler(SyscallOp::SYS_METRICS_ALL_AGENTS,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto agents = ctx_.agent_manager.list_agents();
                json agents_arr = json::array();
                for (const auto& a : agents) {
                    auto m = a->get_metrics();
                    agents_arr.push_back({
                        {"agent_id", m.id},
                        {"name", m.name},
                        {"pid", m.pid},
                        {"state", static_cast<uint8_t>(m.state)},
                        {"memory_bytes", m.memory_bytes},
                        {"uptime_seconds", m.uptime_seconds},
                        {"llm_request_count", m.llm_request_count},
                        {"llm_tokens_used", m.llm_tokens_used}
                    });
                }
                response["success"] = true;
                response["agents"] = agents_arr;
                response["count"] = agents.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_METRICS_ALL_AGENTS, response.dump());
        });

    // SYS_METRICS_CGROUP — read cgroup stats (Linux) or return N/A (macOS)
    router.register_handler(SyscallOp::SYS_METRICS_CGROUP,
        [this](const Message& msg) -> Message {
            json response;
            try {
                response["success"] = true;
#ifdef __APPLE__
                response["available"] = false;
                response["reason"] = "cgroups not available on macOS";
#else
                response["available"] = true;
                // Read cgroup memory stats
                auto read_cgroup_file = [](const std::string& path) -> std::string {
                    std::ifstream f(path);
                    if (!f.is_open()) return "";
                    std::string content;
                    std::getline(f, content);
                    return content;
                };

                std::string mem_current = read_cgroup_file("/sys/fs/cgroup/memory.current");
                std::string mem_max = read_cgroup_file("/sys/fs/cgroup/memory.max");
                std::string cpu_stat = read_cgroup_file("/sys/fs/cgroup/cpu.stat");

                if (!mem_current.empty()) {
                    try { response["memory_current_bytes"] = std::stoull(mem_current); }
                    catch (...) {}
                }
                if (!mem_max.empty() && mem_max != "max") {
                    try { response["memory_max_bytes"] = std::stoull(mem_max); }
                    catch (...) {}
                } else {
                    response["memory_max_bytes"] = "unlimited";
                }
                if (!cpu_stat.empty()) {
                    response["cpu_stat_raw"] = cpu_stat;
                }
#endif
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_METRICS_CGROUP, response.dump());
        });
}

} // namespace clove
