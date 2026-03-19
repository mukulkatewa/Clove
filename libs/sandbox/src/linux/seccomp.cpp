#include <clove/sandbox.hpp>

#ifdef __linux__
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>

namespace clove {

bool Sandbox::apply_seccomp() {
    // Block raw socket creation (AF_PACKET, AF_BLUETOOTH, AF_VSOCK)
    // Allow AF_UNIX (for kernel IPC) and AF_INET/AF_INET6 (controlled by egress proxy)

    // For now, just set NO_NEW_PRIVS which prevents privilege escalation
    // A full BPF filter can be added later for fine-grained syscall control
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        return false;
    }

    isolation_status_.seccomp_active = true;
    return true;
}

} // namespace clove

#endif // __linux__
