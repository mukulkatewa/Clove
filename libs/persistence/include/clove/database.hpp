#pragma once

#include <memory>
#include <mutex>
#include <string>

struct sqlite3;

namespace clove {

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open();
    void close();
    bool is_open() const { return db_ != nullptr; }

    // Execute a statement with no results
    bool exec(const std::string& sql);

    // Run migrations to set up schema
    bool migrate();

    sqlite3* handle() { return db_; }
    std::mutex& mutex() { return mutex_; }

private:
    std::string path_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;

    bool create_schema();
};

} // namespace clove
