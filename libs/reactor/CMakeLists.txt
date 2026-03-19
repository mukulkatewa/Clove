add_library(clove_reactor STATIC
    src/reactor.cpp
)
target_include_directories(clove_reactor PUBLIC include)
target_link_libraries(clove_reactor PUBLIC clove_core)
target_compile_features(clove_reactor PUBLIC cxx_std_23)
