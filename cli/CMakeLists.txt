add_executable(clove_cli
    src/main.cpp
)
target_include_directories(clove_cli PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clove_cli PRIVATE
    clove_core
    nlohmann_json::nlohmann_json
    httplib::httplib
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_features(clove_cli PRIVATE cxx_std_23)
install(TARGETS clove_cli DESTINATION bin)
