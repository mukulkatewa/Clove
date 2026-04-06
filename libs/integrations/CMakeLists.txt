find_package(CURL REQUIRED)

add_library(clove_integrations STATIC
    src/openrouter.cpp
    src/anthropic_client.cpp
    src/mcp_bridge.cpp
    src/a2a_bridge.cpp
    src/tunnel_bridge.cpp
)
target_include_directories(clove_integrations PUBLIC include)
target_link_libraries(clove_integrations PUBLIC
    clove_core
    nlohmann_json::nlohmann_json
    CURL::libcurl
    spdlog::spdlog
)
target_compile_features(clove_integrations PUBLIC cxx_std_23)
