add_executable(clove_kernel
    src/main.cpp
    src/kernel.cpp
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
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_features(clove_kernel PRIVATE cxx_std_23)

install(TARGETS clove_kernel DESTINATION bin)
