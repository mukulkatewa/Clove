#include <catch2/catch_test_macros.hpp>
#include <clove/memory_block.hpp>
#include <clove/memory_block_store.hpp>
#include <clove/context_assembler.hpp>
#include <clove/artifact_store.hpp>
#include <clove/chain_store.hpp>

using namespace clove;

// ---------------------------------------------------------------------------
// MemoryBlock struct tests
// ---------------------------------------------------------------------------

TEST_CASE("MemoryBlock type string conversion", "[memory_blocks]") {
    REQUIRE(std::string(memory_block_type_to_string(MemoryBlockType::SYSTEM)) == "system");
    REQUIRE(std::string(memory_block_type_to_string(MemoryBlockType::CORE)) == "core");
    REQUIRE(std::string(memory_block_type_to_string(MemoryBlockType::RECALL)) == "recall");

    REQUIRE(memory_block_type_from_string("system") == MemoryBlockType::SYSTEM);
    REQUIRE(memory_block_type_from_string("core") == MemoryBlockType::CORE);
    REQUIRE(memory_block_type_from_string("recall") == MemoryBlockType::RECALL);
    REQUIRE(memory_block_type_from_string("invalid") == MemoryBlockType::CORE); // default
}

TEST_CASE("MemoryBlock access string conversion", "[memory_blocks]") {
    REQUIRE(std::string(memory_access_to_string(MemoryAccess::PRIVATE)) == "private");
    REQUIRE(std::string(memory_access_to_string(MemoryAccess::SHARED_READ)) == "shared_read");
    REQUIRE(std::string(memory_access_to_string(MemoryAccess::SHARED_READWRITE)) == "shared_readwrite");

    REQUIRE(memory_access_from_string("private") == MemoryAccess::PRIVATE);
    REQUIRE(memory_access_from_string("shared_read") == MemoryAccess::SHARED_READ);
    REQUIRE(memory_access_from_string("shared_readwrite") == MemoryAccess::SHARED_READWRITE);
}

TEST_CASE("MemoryBlock JSON serialization", "[memory_blocks]") {
    MemoryBlock b;
    b.id = "mem_test123";
    b.name = "persona";
    b.owner_agent_id = 1;
    b.type = MemoryBlockType::CORE;
    b.access = MemoryAccess::SHARED_READ;
    b.content = "I am a research agent.";
    b.shared_with = {2, 3};
    b.created_at_ms = 1000;
    b.updated_at_ms = 2000;

    auto j = memory_block_to_json(b);
    REQUIRE(j["id"] == "mem_test123");
    REQUIRE(j["name"] == "persona");
    REQUIRE(j["type"] == "core");
    REQUIRE(j["access"] == "shared_read");
    REQUIRE(j["content"] == "I am a research agent.");
    REQUIRE(j["shared_with"].size() == 2);
}

// ---------------------------------------------------------------------------
// MemoryBlockStore tests
// ---------------------------------------------------------------------------

TEST_CASE("MemoryBlockStore create and get", "[memory_blocks]") {
    MemoryBlockStore store;

    auto block = store.create(1, "task_state", MemoryBlockType::CORE,
                               MemoryAccess::PRIVATE, "initial state");

    REQUIRE(block.id.substr(0, 4) == "mem_");
    REQUIRE(block.name == "task_state");
    REQUIRE(block.owner_agent_id == 1);
    REQUIRE(block.content == "initial state");

    // Owner can read
    auto opt = store.get(block.id, 1);
    REQUIRE(opt.has_value());
    REQUIRE(opt->content == "initial state");

    // Non-owner cannot read PRIVATE block
    auto opt2 = store.get(block.id, 2);
    REQUIRE_FALSE(opt2.has_value());
}

TEST_CASE("MemoryBlockStore write and append", "[memory_blocks]") {
    MemoryBlockStore store;

    auto block = store.create(1, "notes", MemoryBlockType::CORE,
                               MemoryAccess::PRIVATE, "line 1\n");

    // Write overwrites
    REQUIRE(store.write(block.id, "new content", 1));
    auto opt = store.get(block.id, 1);
    REQUIRE(opt->content == "new content");

    // Append adds to end
    REQUIRE(store.append(block.id, " + more", 1));
    opt = store.get(block.id, 1);
    REQUIRE(opt->content == "new content + more");

    // Non-owner cannot write PRIVATE
    REQUIRE_FALSE(store.write(block.id, "hacked", 2));
}

TEST_CASE("MemoryBlockStore shared access", "[memory_blocks]") {
    MemoryBlockStore store;

    auto block = store.create(1, "shared_data", MemoryBlockType::CORE,
                               MemoryAccess::SHARED_READ, "shared info");

    // Share with agent 2
    REQUIRE(store.share(block.id, 2, 1));

    // Agent 2 can read
    auto opt = store.get(block.id, 2);
    REQUIRE(opt.has_value());
    REQUIRE(opt->content == "shared info");

    // Agent 2 cannot write (SHARED_READ, not READWRITE)
    REQUIRE_FALSE(store.write(block.id, "modified", 2));

    // Agent 3 (not shared) cannot read
    auto opt3 = store.get(block.id, 3);
    REQUIRE_FALSE(opt3.has_value());
}

TEST_CASE("MemoryBlockStore shared readwrite", "[memory_blocks]") {
    MemoryBlockStore store;

    auto block = store.create(1, "collab", MemoryBlockType::CORE,
                               MemoryAccess::SHARED_READWRITE, "start");

    REQUIRE(store.share(block.id, 2, 1));

    // Agent 2 can write
    REQUIRE(store.write(block.id, "agent 2 wrote this", 2));
    auto opt = store.get(block.id, 1);
    REQUIRE(opt->content == "agent 2 wrote this");
}

TEST_CASE("MemoryBlockStore cannot share PRIVATE blocks", "[memory_blocks]") {
    MemoryBlockStore store;

    auto block = store.create(1, "secret", MemoryBlockType::CORE,
                               MemoryAccess::PRIVATE, "private data");

    REQUIRE_FALSE(store.share(block.id, 2, 1));
}

TEST_CASE("MemoryBlockStore delete (owner only)", "[memory_blocks]") {
    MemoryBlockStore store;

    auto block = store.create(1, "temp", MemoryBlockType::RECALL,
                               MemoryAccess::PRIVATE, "temp data");

    REQUIRE_FALSE(store.remove(block.id, 2)); // non-owner
    REQUIRE(store.remove(block.id, 1));        // owner
    REQUIRE_FALSE(store.get(block.id, 1).has_value());
}

TEST_CASE("MemoryBlockStore list shows owned + shared", "[memory_blocks]") {
    MemoryBlockStore store;

    store.create(1, "own1", MemoryBlockType::CORE, MemoryAccess::PRIVATE, "a");
    auto shared = store.create(2, "shared1", MemoryBlockType::CORE,
                                MemoryAccess::SHARED_READ, "b");
    store.share(shared.id, 1, 2);
    store.create(3, "other", MemoryBlockType::CORE, MemoryAccess::PRIVATE, "c");

    auto list = store.list(1);
    REQUIRE(list.size() == 2); // own1 + shared1, not "other"
}

TEST_CASE("MemoryBlockStore get_assembly_blocks returns SYSTEM and CORE", "[memory_blocks]") {
    MemoryBlockStore store;

    store.create(1, "persona", MemoryBlockType::SYSTEM, MemoryAccess::PRIVATE, "I am agent 1");
    store.create(1, "task", MemoryBlockType::CORE, MemoryAccess::PRIVATE, "current task");
    store.create(1, "reference", MemoryBlockType::RECALL, MemoryAccess::PRIVATE, "ref data");

    auto blocks = store.get_assembly_blocks(1);
    REQUIRE(blocks.size() == 2); // SYSTEM + CORE, not RECALL

    // SYSTEM first
    REQUIRE(blocks[0].type == MemoryBlockType::SYSTEM);
    REQUIRE(blocks[1].type == MemoryBlockType::CORE);
}

TEST_CASE("MemoryBlockStore max_tokens enforcement", "[memory_blocks]") {
    MemoryBlockStore store;

    // 10 tokens = 40 chars
    auto block = store.create(1, "limited", MemoryBlockType::CORE,
                               MemoryAccess::PRIVATE, "", 10);

    // Within limit (< 40 chars)
    REQUIRE(store.write(block.id, "short", 1));

    // Exceeds limit (> 40 chars)
    std::string long_text(200, 'x'); // 200 chars = 50 tokens > 10
    REQUIRE_FALSE(store.write(block.id, long_text, 1));
}

TEST_CASE("MemoryBlockStore SYSTEM blocks cannot be edited by non-owner", "[memory_blocks]") {
    MemoryBlockStore store;

    // Create SYSTEM block as agent 0 (kernel)
    auto block = store.create(0, "system_prompt", MemoryBlockType::SYSTEM,
                               MemoryAccess::SHARED_READ, "You are an AI.");

    store.share(block.id, 1, 0);

    // Agent 1 can read
    auto opt = store.get(block.id, 1);
    REQUIRE(opt.has_value());

    // Agent 1 cannot write SYSTEM block
    REQUIRE_FALSE(store.write(block.id, "hacked system", 1));
}

TEST_CASE("MemoryBlockStore get_by_name", "[memory_blocks]") {
    MemoryBlockStore store;

    store.create(1, "persona", MemoryBlockType::CORE, MemoryAccess::PRIVATE, "researcher");
    store.create(1, "task", MemoryBlockType::CORE, MemoryAccess::PRIVATE, "find papers");

    auto opt = store.get_by_name("persona", 1);
    REQUIRE(opt.has_value());
    REQUIRE(opt->content == "researcher");

    auto opt2 = store.get_by_name("nonexistent", 1);
    REQUIRE_FALSE(opt2.has_value());
}

// ---------------------------------------------------------------------------
// Context Assembly with Memory Blocks
// ---------------------------------------------------------------------------

TEST_CASE("ContextAssembler includes memory blocks", "[memory_blocks]") {
    ArtifactStore artifacts;
    ChainStore chains;
    MemoryBlockStore blocks;

    // Create blocks for agent 1
    blocks.create(1, "persona", MemoryBlockType::SYSTEM, MemoryAccess::PRIVATE,
                  "You are a research agent.");
    blocks.create(1, "findings", MemoryBlockType::CORE, MemoryAccess::PRIVATE,
                  "Found 3 relevant papers.");

    // Create a chain with one artifact
    auto chain = chains.create(1, "test chain", "test");
    auto art = artifacts.create(1, ArtifactType::RESEARCH, "Paper Review",
                                 "The paper discusses...", chain.id);
    // Mark as FINAL so it shows in shared context
    artifacts.update_state(art.id, ArtifactState::IN_REVIEW, 1);
    artifacts.update_state(art.id, ArtifactState::APPROVED, 1);
    artifacts.update_state(art.id, ArtifactState::FINAL, 1);

    chains.add_artifact(chain.id, art.id);

    ContextAssembler assembler(artifacts, chains, &blocks);
    auto result = assembler.assemble(1, chain.id);

    // Should contain SYSTEM block content
    REQUIRE(result.context.find("You are a research agent.") != std::string::npos);
    // Should contain CORE block content
    REQUIRE(result.context.find("Found 3 relevant papers.") != std::string::npos);
    // Should contain artifact content
    REQUIRE(result.context.find("The paper discusses...") != std::string::npos);

    REQUIRE(result.blocks_included >= 2);
    REQUIRE(result.artifacts_included >= 1);
}

TEST_CASE("ContextAssembler observation masking", "[memory_blocks]") {
    ArtifactStore artifacts;
    ChainStore chains;

    auto chain = chains.create(1, "test", "");
    auto art = artifacts.create(1, ArtifactType::NOTE, "Tool Output",
                                 std::string(1000, 'x'), chain.id, {},
                                 {{"tool_output", true}});
    artifacts.update_state(art.id, ArtifactState::IN_REVIEW, 1);
    artifacts.update_state(art.id, ArtifactState::APPROVED, 1);
    artifacts.update_state(art.id, ArtifactState::FINAL, 1);
    chains.add_artifact(chain.id, art.id);

    ContextAssembler assembler(artifacts, chains);

    // With compression
    AssemblyConfig config;
    config.compress_observations = true;
    auto result = assembler.assemble(1, chain.id, config);

    // Should NOT contain the full 1000 'x' chars
    REQUIRE(result.context.find(std::string(100, 'x')) == std::string::npos);
    // Should contain the compressed summary
    REQUIRE(result.context.find("1000 chars") != std::string::npos);
}
