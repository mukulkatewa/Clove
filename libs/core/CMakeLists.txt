add_library(clove_core STATIC
    src/protocol.cpp
    src/config.cpp
    src/types.cpp
)
target_include_directories(clove_core PUBLIC include)
target_link_libraries(clove_core PUBLIC nlohmann_json::nlohmann_json fmt::fmt)
target_compile_features(clove_core PUBLIC cxx_std_23)
