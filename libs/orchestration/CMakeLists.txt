add_library(clove_orchestration STATIC
    src/mailbox.cpp
    src/state_store.cpp
    src/event_bus.cpp
    src/llm_queue.cpp
    src/async_tasks.cpp
    src/agent_scheduler.cpp
)
target_include_directories(clove_orchestration PUBLIC include)
target_link_libraries(clove_orchestration PUBLIC clove_core nlohmann_json::nlohmann_json Threads::Threads)
target_compile_features(clove_orchestration PUBLIC cxx_std_23)
