cmake_minimum_required(VERSION 3.20)
project(Clove VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Dependencies
find_package(fmt QUIET)
if(NOT fmt_FOUND)
    FetchContent_Declare(fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 10.2.1
    )
    FetchContent_MakeAvailable(fmt)
endif()

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )
    FetchContent_MakeAvailable(spdlog)
endif()

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(json)
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# Libraries (build order = dependency order)
add_subdirectory(libs/core)
add_subdirectory(libs/reactor)
add_subdirectory(libs/ipc)
add_subdirectory(libs/sandbox)
add_subdirectory(libs/agents)
add_subdirectory(libs/orchestration)
add_subdirectory(libs/governance)
add_subdirectory(libs/integrations)
add_subdirectory(libs/worlds)
add_subdirectory(libs/api)
add_subdirectory(libs/persistence)

# Binaries
add_subdirectory(kernel)
add_subdirectory(cli)

# Tests
enable_testing()
add_subdirectory(tests)
