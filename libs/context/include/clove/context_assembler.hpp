#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace clove {

class ArtifactStore;
class ChainStore;
class MemoryBlockStore;

struct AssemblyConfig {
    bool include_shared  = true;   // L1: FINAL/APPROVED artifacts in chain
    bool include_chain   = true;   // L2: artifacts in agent's lineage
    bool include_private = true;   // L3: agent's own DRAFTs
    bool include_blocks  = true;   // Memory blocks (SYSTEM/CORE always, RECALL if relevant)
    bool compress_observations = true;  // Mask verbose tool outputs
    size_t max_tokens    = 128000; // rough limit (chars / 4)
};

struct AssemblyResult {
    std::string context;           // assembled markdown
    size_t token_estimate = 0;     // estimated tokens
    size_t artifacts_included = 0;
    size_t artifacts_total = 0;
    size_t blocks_included = 0;    // memory blocks included
    bool truncated = false;        // true if budget was exhausted
};

class ContextAssembler {
public:
    ContextAssembler(ArtifactStore& artifacts, ChainStore& chains,
                     MemoryBlockStore* blocks = nullptr);

    // Assemble context for an agent.
    // Order: SYSTEM blocks → CORE blocks → shared artifacts → RECALL blocks → private artifacts
    // Position-aware: critical info at boundaries, less important in middle.
    AssemblyResult assemble(uint32_t agent_id, const std::string& chain_id,
                            const AssemblyConfig& config = {}) const;

    // Estimate token count from text (chars / 4).
    static size_t estimate_tokens(const std::string& text);

    // Compress an observation (tool output) to a summary line.
    static std::string compress_observation(const std::string& title,
                                             const std::string& content);

private:
    ArtifactStore& artifacts_;
    ChainStore& chains_;
    MemoryBlockStore* blocks_;
};

} // namespace clove
