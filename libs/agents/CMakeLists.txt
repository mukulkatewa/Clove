add_library(clove_agents STATIC
    src/agent_process.cpp
    src/agent_manager.cpp
)
target_include_directories(clove_agents PUBLIC include)
target_link_libraries(clove_agents PUBLIC clove_core clove_sandbox)
target_compile_features(clove_agents PUBLIC cxx_std_23)
