add_library(clove_governance STATIC
    src/permissions.cpp
    src/permissions_store.cpp
    src/inference_gateway.cpp
    src/privacy_filter.cpp
    src/audit_log.cpp
    src/execution_log.cpp
    src/policy_watcher.cpp
    src/policy_recommender.cpp
    src/manifest.cpp
)

target_include_directories(clove_governance PUBLIC include)

target_link_libraries(clove_governance PUBLIC
    clove_core
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_features(clove_governance PUBLIC cxx_std_23)
