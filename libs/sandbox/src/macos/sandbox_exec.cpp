/**
 * macOS sandbox-exec (Seatbelt) implementation
 *
 * Apple's kernel-enforced sandbox. No root needed. No code signing needed.
 * Works on all macOS versions (10.5+). Technically deprecated by Apple but
 * still fully functional — there's no replacement for CLI tools.
 *
 * We generate a Seatbelt profile string from SandboxConfig, then call
 * sandbox_init() in the child process before execvp(). The kernel enforces
 * the restrictions — the child process cannot escape them.
 *
 * Restrictions:
 *   - File: deny all read/write except explicitly allowed paths
 *   - Network: deny all outbound except allowed (when enable_network=false)
 *   - Process: allow fork/exec (needed for Node.js/OpenClaw)
 *   - System: read-only access to /usr, /System, /Library (libs + frameworks)
 */

#ifdef __APPLE__

#include <clove/sandbox.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

// Apple's sandbox API — not in public headers but available via libsandbox
extern "C" {
    int sandbox_init(const char *profile, uint64_t flags, char **errorbuf);
    void sandbox_free_error(char *errorbuf);
}

// sandbox_init flags
#define SANDBOX_NAMED    0x0001
#define SANDBOX_NAMED_BUILTIN 0x0002
// Use raw profile string (not named)
#define SANDBOX_NAMED_EXTERNAL 0x0003

namespace clove {

std::string Sandbox::generate_seatbelt_profile() const {
    std::ostringstream sb;

    sb << "(version 1)\n";

    // Default: deny everything
    sb << "(deny default)\n";

    // Allow basic process operations (needed for Node.js runtime)
    sb << "(allow process-fork)\n";
    sb << "(allow process-exec)\n";
    sb << "(allow signal)\n";

    // Allow sysctl reads (Node.js checks CPU count, memory, etc.)
    sb << "(allow sysctl-read)\n";

    // Allow mach-* (needed for IPC, dyld, system frameworks)
    sb << "(allow mach-lookup)\n";
    sb << "(allow mach-register)\n";
    sb << "(allow mach-task-name)\n";
    sb << "(allow ipc-posix-shm-read-data)\n";
    sb << "(allow ipc-posix-shm-write-data)\n";
    sb << "(allow ipc-posix-shm-write-create)\n";

    // Allow reading system libraries and frameworks (read-only)
    sb << "(allow file-read*\n";
    sb << "  (subpath \"/usr/lib\")\n";
    sb << "  (subpath \"/usr/share\")\n";
    sb << "  (subpath \"/System\")\n";
    sb << "  (subpath \"/Library/Frameworks\")\n";
    sb << "  (subpath \"/private/var/db\")\n";  // dyld shared cache
    sb << "  (subpath \"/dev\")\n";              // /dev/null, /dev/urandom
    sb << "  (subpath \"/etc\")\n";              // resolv.conf, hosts
    sb << "  (subpath \"/var\")\n";              // /var/run, /var/folders
    sb << "  (subpath \"/private/tmp\")\n";
    sb << "  (subpath \"/tmp\")\n";
    sb << ")\n";

    // Allow Homebrew paths (Node.js, OpenClaw often installed here)
    sb << "(allow file-read*\n";
    sb << "  (subpath \"/usr/local\")\n";
    sb << "  (subpath \"/opt/homebrew\")\n";
    sb << ")\n";

    // Allow reading the user's home dir for Node.js + OpenClaw config
    const char* home = getenv("HOME");
    if (home) {
        std::string h(home);

        // Node.js needs to read its modules
        sb << "(allow file-read*\n";
        sb << "  (subpath \"" << h << "/.nvm\")\n";
        sb << "  (subpath \"" << h << "/.node\")\n";
        sb << "  (subpath \"" << h << "/.npm\")\n";
        sb << "  (subpath \"" << h << "/.config\")\n";
        sb << ")\n";
    }

    // Allow configured readable paths
    if (!config_.allowed_paths.empty()) {
        sb << "(allow file-read*\n";
        for (const auto& path : config_.allowed_paths) {
            sb << "  (subpath \"" << path << "\")\n";
        }
        sb << ")\n";
    }

    // Allow configured writable paths
    if (!config_.writable_paths.empty()) {
        sb << "(allow file-write*\n";
        for (const auto& path : config_.writable_paths) {
            sb << "  (subpath \"" << path << "\")\n";
        }
        sb << ")\n";
    }

    // Always allow writing to /tmp and /private/tmp
    sb << "(allow file-write*\n";
    sb << "  (subpath \"/tmp\")\n";
    sb << "  (subpath \"/private/tmp\")\n";
    sb << "  (subpath \"/dev/null\")\n";
    sb << ")\n";

    // Allow writing to /var/folders (macOS temp dirs)
    sb << "(allow file-write*\n";
    sb << "  (subpath \"/private/var/folders\")\n";
    sb << "  (subpath \"/var/folders\")\n";
    sb << ")\n";

    // Network access
    if (config_.enable_network) {
        // Allow all network when enabled
        sb << "(allow network*)\n";
    } else {
        // Deny network — only allow localhost for kernel IPC
        sb << "(allow network* (local ip \"localhost:*\"))\n";
        sb << "(allow network* (remote ip \"localhost:*\"))\n";
        // Allow DNS resolution
        sb << "(allow network-outbound (remote unix-socket (path-literal \"/var/run/mDNSResponder\")))\n";
    }

    return sb.str();
}

bool Sandbox::apply_seatbelt() {
    std::string profile = generate_seatbelt_profile();

    spdlog::debug("Seatbelt profile for {}:\n{}", config_.name, profile);

    char* errorbuf = nullptr;
    int rc = sandbox_init(profile.c_str(), 0, &errorbuf);

    if (rc != 0) {
        std::string err_msg = errorbuf ? errorbuf : "unknown error";
        if (errorbuf) sandbox_free_error(errorbuf);
        spdlog::error("sandbox_init() failed for {}: {}", config_.name, err_msg);
        return false;
    }

    spdlog::info("Seatbelt sandbox applied for {}", config_.name);
    isolation_status_.fully_isolated = true;

    return true;
}

} // namespace clove

#endif // __APPLE__
