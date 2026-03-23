add_library(clove_context STATIC
    src/artifact_store.cpp
    src/chain_store.cpp
    src/context_assembler.cpp
    src/memory_block_store.cpp
)
target_include_directories(clove_context PUBLIC include)
target_link_libraries(clove_context PUBLIC
    clove_core
    nlohmann_json::nlohmann_json
)
target_compile_features(clove_context PUBLIC cxx_std_23)
