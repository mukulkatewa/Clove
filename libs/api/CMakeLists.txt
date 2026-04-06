add_library(clove_api STATIC
    src/api_server.cpp
    src/run_engine.cpp
    src/openclaw_manager.cpp
    src/daemon_manager.cpp
    src/job_queue.cpp
)
target_include_directories(clove_api PUBLIC include)
target_link_libraries(clove_api PUBLIC
    clove_core
    clove_agents
    clove_sandbox
    clove_orchestration
    clove_governance
    clove_integrations
    clove_worlds
    clove_context
    clove_persistence
    clove_memory
    nlohmann_json::nlohmann_json
    httplib::httplib
    spdlog::spdlog
    Threads::Threads
)
target_compile_features(clove_api PUBLIC cxx_std_23)
