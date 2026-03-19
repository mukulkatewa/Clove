// macOS sandbox-exec (Seatbelt) profiles
// This provides kernel-enforced filesystem + network restrictions on macOS
// Not as strong as Linux namespaces, but significantly better than fork-only
//
// Note: sandbox-exec is technically deprecated by Apple but still functional
// on all current macOS versions. The alternative (App Sandbox via entitlements)
// requires code signing and is designed for GUI apps.
//
// Implementation deferred to Phase 3 — requires testing on target macOS versions
// For now, macOS uses fork() + egress proxy env vars
