#include "mod.hpp"
#include <clove/artifact_store.hpp>
#include <clove/chain_store.hpp>
#include <clove/context_assembler.hpp>
#include <clove/artifact_store_db.hpp>
#include <unordered_set>

namespace clove {

void ContextSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_DOC_CREATE — create an artifact in a chain
    router.register_handler(SyscallOp::SYS_DOC_CREATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string type_str = req.at("type").get<std::string>();
                std::string title = req.at("title").get<std::string>();
                std::string content = req.at("content").get<std::string>();
                std::string chain_id = req.at("chain_id").get<std::string>();

                std::vector<std::string> parent_ids;
                if (req.contains("parent_ids")) {
                    parent_ids = req["parent_ids"].get<std::vector<std::string>>();
                }
                json metadata = req.value("metadata", json::object());

                auto artifact = artifacts_->create(
                    msg.agent_id(), artifact_type_from_string(type_str),
                    title, content, chain_id, parent_ids, metadata);

                // Auto-add to chain
                chains_->add_artifact(chain_id, artifact.id);

                // Persist
                if (db_) {
                    db_->store(artifact);
                    auto chain_opt = chains_->get(chain_id);
                    if (chain_opt) db_->store_chain(*chain_opt);
                }

                response["success"] = true;
                response["artifact"] = artifact_to_json(artifact);
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_CREATE, response.dump());
        });

    // SYS_DOC_READ — read an artifact by ID
    router.register_handler(SyscallOp::SYS_DOC_READ,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();

                auto opt = artifacts_->get(id);
                if (opt) {
                    response["success"] = true;
                    response["artifact"] = artifact_to_json(*opt);
                } else {
                    response["success"] = false;
                    response["error"] = "artifact not found: " + id;
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_READ, response.dump());
        });

    // SYS_DOC_UPDATE — update content and/or state
    router.register_handler(SyscallOp::SYS_DOC_UPDATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();

                bool updated = false;
                if (req.contains("content")) {
                    std::string content = req["content"].get<std::string>();
                    if (!artifacts_->update_content(id, content, msg.agent_id())) {
                        response["success"] = false;
                        response["error"] = "failed to update content (not found or not owner)";
                        return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_UPDATE, response.dump());
                    }
                    updated = true;
                }

                if (req.contains("state")) {
                    std::string state_str = req["state"].get<std::string>();
                    ArtifactState new_state = artifact_state_from_string(state_str);
                    if (!artifacts_->update_state(id, new_state, msg.agent_id())) {
                        response["success"] = false;
                        response["error"] = "invalid state transition or not found";
                        return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_UPDATE, response.dump());
                    }
                    updated = true;
                }

                if (updated) {
                    auto opt = artifacts_->get(id);
                    response["success"] = true;
                    if (opt) {
                        response["artifact"] = artifact_to_json(*opt);
                        if (db_) db_->store(*opt);
                    }
                } else {
                    response["success"] = false;
                    response["error"] = "no fields to update";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_UPDATE, response.dump());
        });

    // SYS_DOC_LIST — list artifacts with filters
    router.register_handler(SyscallOp::SYS_DOC_LIST,
        [this](const Message& msg) -> Message {
            json response;
            try {
                ArtifactFilter filter;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    if (req.contains("chain_id"))
                        filter.chain_id = req["chain_id"].get<std::string>();
                    if (req.contains("type"))
                        filter.type = artifact_type_from_string(req["type"].get<std::string>());
                    if (req.contains("state"))
                        filter.state = artifact_state_from_string(req["state"].get<std::string>());
                    if (req.contains("author_agent_id"))
                        filter.author_agent_id = req["author_agent_id"].get<uint32_t>();
                    filter.limit = std::min(
                        req.value("limit", size_t(100)),
                        ArtifactFilter::MAX_LIMIT);
                }

                auto arts = artifacts_->list(filter);
                json arr = json::array();
                for (const auto& a : arts) {
                    arr.push_back(artifact_to_json(a));
                }
                response["success"] = true;
                response["artifacts"] = arr;
                response["count"] = arts.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_LIST, response.dump());
        });

    // SYS_DOC_DELETE — delete a DRAFT artifact (author only)
    router.register_handler(SyscallOp::SYS_DOC_DELETE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();

                if (artifacts_->remove(id, msg.agent_id())) {
                    response["success"] = true;
                    if (db_) db_->erase(id);
                } else {
                    response["success"] = false;
                    response["error"] = "cannot delete (not found, not owner, or not DRAFT)";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_DOC_DELETE, response.dump());
        });

    // SYS_CHAIN_CREATE — create a new document chain
    router.register_handler(SyscallOp::SYS_CHAIN_CREATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string name = req.at("name").get<std::string>();
                std::string desc = req.value("description", "");
                json metadata = req.value("metadata", json::object());

                auto chain = chains_->create(msg.agent_id(), name, desc, metadata);
                if (db_) db_->store_chain(chain);
                response["success"] = true;
                response["chain"] = chain_to_json(chain);
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_CHAIN_CREATE, response.dump());
        });

    // SYS_CHAIN_GET — get chain with its artifacts and DAG structure
    router.register_handler(SyscallOp::SYS_CHAIN_GET,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();

                auto chain_opt = chains_->get(id);
                if (!chain_opt) {
                    response["success"] = false;
                    response["error"] = "chain not found: " + id;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_CHAIN_GET, response.dump());
                }

                // Fetch all artifacts in this chain
                ArtifactFilter filter;
                filter.chain_id = id;
                filter.limit = 1000;
                auto arts = artifacts_->list(filter);

                // Build artifact ID set for DAG validation
                std::unordered_set<std::string> art_ids;
                for (const auto& a : arts) art_ids.insert(a.id);

                json arts_arr = json::array();
                json edges = json::array();
                for (const auto& a : arts) {
                    arts_arr.push_back(artifact_to_json(a));
                    for (const auto& pid : a.parent_ids) {
                        if (art_ids.count(pid)) {
                            edges.push_back({{"from", pid}, {"to", a.id}});
                        }
                    }
                }

                response["success"] = true;
                response["chain"] = chain_to_json(*chain_opt);
                response["artifacts"] = arts_arr;
                response["dag"] = {{"edges", edges}};
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_CHAIN_GET, response.dump());
        });

    // SYS_CHAIN_FORK — fork a chain at a given artifact
    router.register_handler(SyscallOp::SYS_CHAIN_FORK,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string chain_id = req.at("chain_id").get<std::string>();
                std::string at_artifact_id = req.at("at_artifact_id").get<std::string>();

                auto forked = chains_->fork(chain_id, at_artifact_id, msg.agent_id());
                if (forked) {
                    if (db_) db_->store_chain(*forked);
                    response["success"] = true;
                    response["chain"] = chain_to_json(*forked);
                } else {
                    response["success"] = false;
                    response["error"] = "fork failed (chain or artifact not found)";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_CHAIN_FORK, response.dump());
        });

    // SYS_CONTEXT_ASSEMBLE — assemble context from chain artifacts
    router.register_handler(SyscallOp::SYS_CONTEXT_ASSEMBLE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string chain_id = req.at("chain_id").get<std::string>();

                AssemblyConfig config;
                config.max_tokens = req.value("max_tokens", size_t(128000));
                config.include_shared = req.value("include_shared", true);
                config.include_chain = req.value("include_chain", true);
                config.include_private = req.value("include_private", true);

                auto assembled = assembler_->assemble(msg.agent_id(), chain_id, config);

                response["success"] = true;
                response["context"] = assembled.context;
                response["token_estimate"] = assembled.token_estimate;
                response["artifacts_included"] = assembled.artifacts_included;
                response["artifacts_total"] = assembled.artifacts_total;
                response["truncated"] = assembled.truncated;
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_CONTEXT_ASSEMBLE, response.dump());
        });
}

} // namespace clove
