include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(clove_tests
    test_protocol.cpp
    test_state_store.cpp
    test_mailbox.cpp
    test_event_bus.cpp
    test_permissions.cpp
    test_privacy_filter.cpp
    test_audit_log.cpp
    test_execution_log.cpp
    test_inference_gateway.cpp
    test_policy_recommender.cpp
    test_mcp_bridge.cpp
    test_database.cpp
    test_tunnel_bridge.cpp
    test_world_engine.cpp
    test_a2a_bridge.cpp
    test_execution_replay.cpp
)

target_link_libraries(clove_tests PRIVATE
    Catch2::Catch2WithMain
    clove_core
    clove_orchestration
    clove_governance
    clove_integrations
    clove_persistence
    clove_worlds
    nlohmann_json::nlohmann_json
    Threads::Threads
)

target_compile_features(clove_tests PRIVATE cxx_std_23)

include(CTest)
include(Catch)
catch_discover_tests(clove_tests)
