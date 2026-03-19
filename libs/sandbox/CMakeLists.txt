add_library(clove_sandbox STATIC
    src/sandbox.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(clove_sandbox PRIVATE
        src/linux/landlock.cpp
        src/linux/seccomp.cpp
    )
endif()

target_include_directories(clove_sandbox PUBLIC include)
target_link_libraries(clove_sandbox PUBLIC clove_core spdlog::spdlog)
target_compile_features(clove_sandbox PUBLIC cxx_std_23)
