#include <clove/context_assembler.hpp>
#include <clove/artifact_store.hpp>
#include <clove/chain_store.hpp>
#include <clove/memory_block_store.hpp>
#include <algorithm>
#include <sstream>

namespace clove {

ContextAssembler::ContextAssembler(ArtifactStore& artifacts, ChainStore& chains,
                                    MemoryBlockStore* blocks)
    : artifacts_(artifacts), chains_(chains), blocks_(blocks) {}

std::string ContextAssembler::compress_observation(const std::string& title,
                                                    const std::string& content) {
    // Observation masking: replace verbose tool output with a summary line
    size_t chars = content.size();
    size_t lines = std::count(content.begin(), content.end(), '\n') + 1;
    return "[" + title + ": " + std::to_string(chars) + " chars, " +
           std::to_string(lines) + " lines]";
}

AssemblyResult ContextAssembler::assemble(uint32_t agent_id, const std::string& chain_id,
                                           const AssemblyConfig& config) const {
    AssemblyResult result;
    size_t budget = config.max_tokens;
    std::ostringstream out;

    // Helper: try to append text, respecting budget
    auto try_append = [&](const std::string& text) -> bool {
        size_t tokens = estimate_tokens(text);
        if (budget < tokens) {
            result.truncated = true;
            return false;
        }
        budget -= tokens;
        out << text;
        return true;
    };

    // ─── Phase 1: SYSTEM memory blocks (pinned, always first) ─────────
    if (config.include_blocks && blocks_) {
        auto blocks = blocks_->get_assembly_blocks(agent_id);
        for (const auto& b : blocks) {
            if (b.type == MemoryBlockType::SYSTEM) {
                // SYSTEM blocks are not counted against budget
                out << "## [SYSTEM] " << b.name << "\n" << b.content << "\n\n";
                result.blocks_included++;
            }
        }

        // ─── Phase 2: CORE memory blocks (always included, counted) ──────
        for (const auto& b : blocks) {
            if (b.type == MemoryBlockType::CORE) {
                std::string entry = "## [CORE] " + b.name + "\n" + b.content + "\n\n";
                if (!try_append(entry)) break;
                result.blocks_included++;
            }
        }
    }

    // ─── Phase 3: Shared artifacts (FINAL/APPROVED from chain) ────────
    if (config.include_shared && !chain_id.empty()) {
        auto chain_opt = chains_.get(chain_id);
        if (chain_opt) {
            ArtifactFilter filter;
            filter.chain_id = chain_id;
            filter.limit = 1000;
            auto all_artifacts = artifacts_.list(filter);

            result.artifacts_total = all_artifacts.size();

            // Collect shared artifacts
            std::vector<Artifact> shared;
            std::vector<Artifact> priv;

            for (const auto& a : all_artifacts) {
                if (a.state == ArtifactState::FINAL || a.state == ArtifactState::APPROVED) {
                    shared.push_back(a);
                }
                if (a.author_agent_id == agent_id && a.state == ArtifactState::DRAFT &&
                    config.include_private) {
                    priv.push_back(a);
                }
            }

            // Append shared artifacts header
            if (!shared.empty()) {
                try_append("\n## Shared Context\n\n");

                for (const auto& a : shared) {
                    std::string content = a.content;

                    // Observation masking: compress NOTE artifacts with tool output
                    if (config.compress_observations &&
                        a.type == ArtifactType::NOTE &&
                        a.metadata.contains("tool_output") &&
                        a.metadata["tool_output"].get<bool>()) {
                        content = compress_observation(a.title, a.content);
                    }

                    std::string entry = "### " + a.title + " (" +
                                        artifact_type_to_string(a.type) + ")\n" +
                                        content + "\n\n";
                    if (!try_append(entry)) break;
                    result.artifacts_included++;
                }
            }

            // ─── Phase 4: RECALL memory blocks (if space remains) ─────────
            if (config.include_blocks && blocks_) {
                auto recall_blocks = blocks_->list(agent_id, 50);
                for (const auto& b : recall_blocks) {
                    if (b.type == MemoryBlockType::RECALL) {
                        std::string entry = "## [RECALL] " + b.name + "\n" + b.content + "\n\n";
                        if (!try_append(entry)) break;
                        result.blocks_included++;
                    }
                }
            }

            // ─── Phase 5: Private artifacts (DRAFT by this agent) ─────────
            if (!priv.empty()) {
                try_append("\n## Private Notes\n\n");

                for (const auto& a : priv) {
                    std::string entry = "### " + a.title + "\n" + a.content + "\n\n";
                    if (!try_append(entry)) break;
                    result.artifacts_included++;
                }
            }
        }
    }

    result.context = out.str();
    result.token_estimate = estimate_tokens(result.context);
    return result;
}

size_t ContextAssembler::estimate_tokens(const std::string& text) {
    return text.size() / 4;
}

} // namespace clove
