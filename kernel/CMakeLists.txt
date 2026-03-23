add_executable(clove_kernel
    src/main.cpp
    src/kernel.cpp
    src/syscall_router.cpp
    src/syscalls/state_syscalls.cpp
    src/syscalls/ipc_syscalls.cpp
    src/syscalls/event_syscalls.cpp
    src/syscalls/permission_syscalls.cpp
    src/syscalls/audit_syscalls.cpp
    src/syscalls/llm_syscalls.cpp
    src/syscalls/pii_syscalls.cpp
    src/syscalls/replay_syscalls.cpp
    src/syscalls/agent_syscalls.cpp
    src/syscalls/async_syscalls.cpp
    src/syscalls/network_syscalls.cpp
    src/syscalls/fileio_syscalls.cpp
    src/syscalls/metrics_syscalls.cpp
    src/syscalls/mcp_syscalls.cpp
    src/syscalls/a2a_syscalls.cpp
    src/syscalls/otel_syscalls.cpp
    src/syscalls/creds_syscalls.cpp
    src/syscalls/tunnel_syscalls.cpp
    src/syscalls/world_syscalls.cpp
    src/syscalls/context_syscalls.cpp
    src/syscalls/budget_syscalls.cpp
    src/syscalls/memory_syscalls.cpp
)

target_include_directories(clove_kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}   # For kernel/src/context.hpp etc.
)

target_link_libraries(clove_kernel PRIVATE
    clove_core
    clove_reactor
    clove_ipc
    clove_sandbox
    clove_agents
    clove_orchestration
    clove_governance
    clove_integrations
    clove_worlds
    clove_api
    clove_context
    clove_persistence
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_features(clove_kernel PRIVATE cxx_std_23)

install(TARGETS clove_kernel DESTINATION bin)
