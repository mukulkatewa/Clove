find_package(SQLite3 REQUIRED)

add_library(clove_persistence STATIC
    src/database.cpp
    src/audit_store.cpp
    src/state_store_db.cpp
)
target_include_directories(clove_persistence PUBLIC include)
target_link_libraries(clove_persistence PUBLIC
    clove_core
    clove_governance
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)
target_compile_features(clove_persistence PUBLIC cxx_std_23)
