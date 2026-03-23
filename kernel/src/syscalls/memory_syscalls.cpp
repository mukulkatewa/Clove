#include "mod.hpp"
#include <clove/memory_block_store.hpp>
#include <clove/memory_block_db.hpp>
#include <clove/audit_log.hpp>

namespace clove {

void MemorySyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_MEM_CREATE — create a named memory block
    router.register_handler(SyscallOp::SYS_MEM_CREATE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string name = req.at("name").get<std::string>();
                std::string type_str = req.value("type", "core");
                std::string access_str = req.value("access", "private");
                std::string content = req.value("content", "");
                size_t max_tokens = req.value("max_tokens", size_t(0));

                auto block = store_->create(
                    msg.agent_id(), name,
                    memory_block_type_from_string(type_str),
                    memory_access_from_string(access_str),
                    content, max_tokens);

                if (db_) db_->store(block);

                ctx_.audit_logger.log(AuditCategory::STATE_STORE, "MEM_BLOCK_CREATED",
                    msg.agent_id(), "", {{"block_id", block.id}, {"name", name}});

                response["success"] = true;
                response["block"] = memory_block_to_json(block);
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_CREATE, response.dump());
        });

    // SYS_MEM_READ — read a memory block
    router.register_handler(SyscallOp::SYS_MEM_READ,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.value("id", "");
                std::string name = req.value("name", "");

                std::optional<MemoryBlock> opt;
                if (!id.empty()) {
                    opt = store_->get(id, msg.agent_id());
                } else if (!name.empty()) {
                    // Look up by name for this agent
                    uint32_t owner = req.value("owner_agent_id", msg.agent_id());
                    opt = store_->get_by_name(name, owner);
                    // Verify read access
                    if (opt && opt->owner_agent_id != msg.agent_id() &&
                        opt->access == MemoryAccess::PRIVATE) {
                        opt = std::nullopt;
                    }
                }

                if (opt) {
                    response["success"] = true;
                    response["block"] = memory_block_to_json(*opt);
                } else {
                    response["success"] = false;
                    response["error"] = "block not found or no access";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_READ, response.dump());
        });

    // SYS_MEM_WRITE — overwrite block content
    router.register_handler(SyscallOp::SYS_MEM_WRITE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();
                std::string content = req.at("content").get<std::string>();

                if (store_->write(id, content, msg.agent_id())) {
                    auto opt = store_->get(id, msg.agent_id());
                    response["success"] = true;
                    if (opt) {
                        response["block"] = memory_block_to_json(*opt);
                        if (db_) db_->store(*opt);
                    }
                } else {
                    response["success"] = false;
                    response["error"] = "write failed (not found, no access, or exceeds max_tokens)";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_WRITE, response.dump());
        });

    // SYS_MEM_APPEND — append to block content
    router.register_handler(SyscallOp::SYS_MEM_APPEND,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();
                std::string content = req.at("content").get<std::string>();

                if (store_->append(id, content, msg.agent_id())) {
                    auto opt = store_->get(id, msg.agent_id());
                    response["success"] = true;
                    if (opt) {
                        response["block"] = memory_block_to_json(*opt);
                        if (db_) db_->store(*opt);
                    }
                } else {
                    response["success"] = false;
                    response["error"] = "append failed (not found, no access, or exceeds max_tokens)";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_APPEND, response.dump());
        });

    // SYS_MEM_DELETE — delete a memory block (owner only)
    router.register_handler(SyscallOp::SYS_MEM_DELETE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();

                if (store_->remove(id, msg.agent_id())) {
                    if (db_) db_->erase(id);
                    ctx_.audit_logger.log(AuditCategory::STATE_STORE, "MEM_BLOCK_DELETED",
                        msg.agent_id(), "", {{"block_id", id}});
                    response["success"] = true;
                } else {
                    response["success"] = false;
                    response["error"] = "delete failed (not found or not owner)";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_DELETE, response.dump());
        });

    // SYS_MEM_LIST — list memory blocks visible to agent
    router.register_handler(SyscallOp::SYS_MEM_LIST,
        [this](const Message& msg) -> Message {
            json response;
            try {
                size_t limit = 100;
                if (!msg.payload.empty()) {
                    auto req = json::parse(msg.payload_str());
                    limit = req.value("limit", size_t(100));
                }

                auto blocks = store_->list(msg.agent_id(), limit);
                json arr = json::array();
                for (const auto& b : blocks) {
                    arr.push_back(memory_block_to_json(b));
                }
                response["success"] = true;
                response["blocks"] = arr;
                response["count"] = blocks.size();
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_LIST, response.dump());
        });

    // SYS_MEM_SHARE — share a block with another agent
    router.register_handler(SyscallOp::SYS_MEM_SHARE,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string id = req.at("id").get<std::string>();
                uint32_t target_id = req.at("target_agent_id").get<uint32_t>();

                if (store_->share(id, target_id, msg.agent_id())) {
                    ctx_.audit_logger.log(AuditCategory::STATE_STORE, "MEM_BLOCK_SHARED",
                        msg.agent_id(), "", {
                            {"block_id", id}, {"target_agent_id", target_id}
                        });
                    auto opt = store_->get(id, msg.agent_id());
                    response["success"] = true;
                    if (opt) {
                        response["block"] = memory_block_to_json(*opt);
                        if (db_) db_->store(*opt);
                    }
                } else {
                    response["success"] = false;
                    response["error"] = "share failed (not found, not owner, or block is PRIVATE)";
                }
            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_MEM_SHARE, response.dump());
        });
}

} // namespace clove
