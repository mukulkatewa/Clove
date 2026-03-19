add_library(clove_ipc STATIC
    src/socket_server.cpp
    src/socket_client.cpp
)
target_include_directories(clove_ipc PUBLIC include)
target_link_libraries(clove_ipc PUBLIC clove_core)
target_compile_features(clove_ipc PUBLIC cxx_std_23)
