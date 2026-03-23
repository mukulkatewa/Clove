#pragma once

#include <clove/memory_block.hpp>
#include <string>
#include <vector>

namespace clove {

class Database;

class MemoryBlockDb {
public:
    explicit MemoryBlockDb(Database& db);

    bool store(const MemoryBlock& block);
    bool erase(const std::string& id);
    std::vector<MemoryBlock> load_all() const;
    size_t count() const;

private:
    Database& db_;
};

} // namespace clove
