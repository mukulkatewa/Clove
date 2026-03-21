add_library(clove_api STATIC
    src/api_server.cpp
)
target_include_directories(clove_api PUBLIC include)
target_link_libraries(clove_api PUBLIC
    clove_core
    clove_agents
    clove_orchestration
    clove_governance
    clove_integrations
    clove_worlds
    nlohmann_json::nlohmann_json
    httplib::httplib
    Threads::Threads
)
target_compile_features(clove_api PUBLIC cxx_std_23)
