add_library(clove_worlds STATIC
    src/world_engine.cpp
)
target_include_directories(clove_worlds PUBLIC include)
target_link_libraries(clove_worlds PUBLIC clove_core clove_orchestration nlohmann_json::nlohmann_json)
target_compile_features(clove_worlds PUBLIC cxx_std_23)
