#include <clove/sandbox.hpp>

#ifdef __linux__
#include <linux/landlock.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <fcntl.h>

namespace clove {

// Landlock syscall wrappers (not in glibc yet)
static inline int landlock_create_ruleset(
    const struct landlock_ruleset_attr* attr, size_t size, uint32_t flags) {
    return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}

static inline int landlock_add_rule(
    int ruleset_fd, enum landlock_rule_type type, const void* attr, uint32_t flags) {
    return syscall(__NR_landlock_add_rule, ruleset_fd, type, attr, flags);
}

static inline int landlock_restrict_self(int ruleset_fd, uint32_t flags) {
    return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}

bool Sandbox::apply_landlock() {
    // Check if Landlock is supported
    struct landlock_ruleset_attr ruleset_attr = {
        .handled_access_fs =
            LANDLOCK_ACCESS_FS_READ_FILE |
            LANDLOCK_ACCESS_FS_READ_DIR |
            LANDLOCK_ACCESS_FS_WRITE_FILE |
            LANDLOCK_ACCESS_FS_REMOVE_FILE |
            LANDLOCK_ACCESS_FS_REMOVE_DIR |
            LANDLOCK_ACCESS_FS_MAKE_REG |
            LANDLOCK_ACCESS_FS_MAKE_DIR |
            LANDLOCK_ACCESS_FS_EXECUTE,
    };

    int ruleset_fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
    if (ruleset_fd < 0) {
        return false;  // Landlock not supported
    }

    // Add rules for allowed paths (read-only)
    for (const auto& path : config_.allowed_paths) {
        int path_fd = open(path.c_str(), O_PATH | O_CLOEXEC);
        if (path_fd < 0) continue;

        struct landlock_path_beneath_attr path_attr = {
            .allowed_access =
                LANDLOCK_ACCESS_FS_READ_FILE |
                LANDLOCK_ACCESS_FS_READ_DIR |
                LANDLOCK_ACCESS_FS_EXECUTE,
            .parent_fd = path_fd,
        };
        landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_attr, 0);
        close(path_fd);
    }

    // Add rules for writable paths
    for (const auto& path : config_.writable_paths) {
        int path_fd = open(path.c_str(), O_PATH | O_CLOEXEC);
        if (path_fd < 0) continue;

        struct landlock_path_beneath_attr path_attr = {
            .allowed_access =
                LANDLOCK_ACCESS_FS_READ_FILE |
                LANDLOCK_ACCESS_FS_READ_DIR |
                LANDLOCK_ACCESS_FS_WRITE_FILE |
                LANDLOCK_ACCESS_FS_REMOVE_FILE |
                LANDLOCK_ACCESS_FS_REMOVE_DIR |
                LANDLOCK_ACCESS_FS_MAKE_REG |
                LANDLOCK_ACCESS_FS_MAKE_DIR |
                LANDLOCK_ACCESS_FS_EXECUTE,
            .parent_fd = path_fd,
        };
        landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_attr, 0);
        close(path_fd);
    }

    // Enforce
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        close(ruleset_fd);
        return false;
    }

    if (landlock_restrict_self(ruleset_fd, 0) < 0) {
        close(ruleset_fd);
        return false;
    }

    close(ruleset_fd);
    isolation_status_.landlock_active = true;
    return true;
}

} // namespace clove

#endif // __linux__
