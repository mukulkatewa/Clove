find_package(SQLite3 REQUIRED)

add_library(clove_memory STATIC
    src/memory_store.cpp
    src/memory_manager.cpp
)
target_include_directories(clove_memory PUBLIC include)
target_link_libraries(clove_memory PUBLIC
    clove_persistence
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)
target_compile_features(clove_memory PUBLIC cxx_std_23)
