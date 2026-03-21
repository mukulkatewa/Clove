#include <clove/sandbox.hpp>

#ifdef __linux__
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>

// Guard syscall numbers that may be absent on older kernels
#ifndef __NR_kexec_file_load
#define __NR_kexec_file_load -1
#endif
#ifndef __NR_finit_module
#define __NR_finit_module -1
#endif
#ifndef __NR_userfaultfd
#define __NR_userfaultfd -1
#endif
#ifndef __NR_bpf
#define __NR_bpf -1
#endif
#ifndef __NR_process_vm_readv
#define __NR_process_vm_readv -1
#endif
#ifndef __NR_process_vm_writev
#define __NR_process_vm_writev -1
#endif

// Macro to emit a two-instruction block: if syscall == NR, jump to deny; else fall through.
// The jump offset of 0 means "jump to the very next instruction" (the deny return).
// The skip offset of 1 means "skip one instruction" (skip over the deny return).
#define DENY_SYSCALL(nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA))

namespace clove {

bool Sandbox::apply_seccomp() {
    // Step 1: NO_NEW_PRIVS is required before installing a seccomp filter
    // as a non-root process. It also prevents privilege escalation via
    // execve of setuid/setgid binaries.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
        return false;
    }

    // Step 2: Build the BPF filter program.
    //
    // Strategy: default-allow with an explicit deny list.
    // Agents need standard I/O, networking (AF_UNIX for kernel IPC,
    // AF_INET/AF_INET6 via egress proxy), file I/O, signals, etc.
    // We only block dangerous syscalls that an agent should never need.
    //
    // Each blocked syscall is a pair of instructions:
    //   JEQ check  ->  RET_ERRNO(EPERM)
    // If the check doesn't match, execution falls through to the next pair.
    // The final instruction is a default RET_ALLOW.

    struct sock_filter filter[] = {
        // Load the syscall number from seccomp_data
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, nr)),

        // --- Debugging / cross-process memory access ---
        DENY_SYSCALL(__NR_ptrace),
        DENY_SYSCALL(__NR_process_vm_readv),
        DENY_SYSCALL(__NR_process_vm_writev),

        // --- Kernel replacement ---
        DENY_SYSCALL(__NR_kexec_load),
        DENY_SYSCALL(__NR_kexec_file_load),

        // --- Kernel modules ---
        DENY_SYSCALL(__NR_init_module),
        DENY_SYSCALL(__NR_finit_module),
        DENY_SYSCALL(__NR_delete_module),

        // --- System-level operations ---
        DENY_SYSCALL(__NR_reboot),
        DENY_SYSCALL(__NR_swapon),
        DENY_SYSCALL(__NR_swapoff),

        // --- Filesystem mounting (defense-in-depth; namespaces also block this) ---
        DENY_SYSCALL(__NR_mount),
        DENY_SYSCALL(__NR_umount2),

        // --- Root filesystem changes ---
        DENY_SYSCALL(__NR_pivot_root),
        DENY_SYSCALL(__NR_chroot),

        // --- Process accounting ---
        DENY_SYSCALL(__NR_acct),

        // --- Clock manipulation ---
        DENY_SYSCALL(__NR_settimeofday),
        DENY_SYSCALL(__NR_clock_settime),

        // --- Kernel keyring ---
        DENY_SYSCALL(__NR_add_key),
        DENY_SYSCALL(__NR_request_key),
        DENY_SYSCALL(__NR_keyctl),

        // --- BPF (prevent seccomp bypass / filter injection) ---
        DENY_SYSCALL(__NR_bpf),

        // --- Exploit-prone interfaces ---
        DENY_SYSCALL(__NR_userfaultfd),
        DENY_SYSCALL(__NR_perf_event_open),

        // --- Namespace escape ---
        DENY_SYSCALL(__NR_unshare),
        DENY_SYSCALL(__NR_setns),

        // Default policy: allow everything else
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };

    struct sock_fprog prog = {
        .len = static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    // Step 3: Install the filter
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
        return false;
    }

    isolation_status_.seccomp_active = true;
    return true;
}

} // namespace clove

#endif // __linux__
