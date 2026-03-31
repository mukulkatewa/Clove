# CLOVE CLI — Linux Install & Usage Guide

**Package:** [`@cloveos/cli`](https://www.npmjs.com/package/@cloveos/cli)
**Version:** `0.2.0` (published 2026-03-31)
**Platforms:** Linux + macOS
**npm:** https://www.npmjs.com/package/@cloveos/cli

---

## Quick Start (Linux)

```bash
# 1. Install build dependencies (one-time)
sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev libsqlite3-dev   # Ubuntu/Debian

# 2. Install CLOVE
npm install -g @cloveos/cli

# 3. Set your LLM API key
export OPENROUTER_API_KEY=sk-or-v1-...

# 4. Use it
clove start              # builds kernel + launches + opens dashboard
clove run "your goal"    # run a single agent
clove fleet "goal" -n 3  # run 3 agents in parallel
clove status             # check kernel health
clove stop               # shut down
```

Works the same on macOS and Linux.

---

## Full Installation Guide

### Prerequisites

| Component | Minimum | Check |
|-----------|---------|-------|
| Node.js | >= 22.0.0 | `node --version` |
| npm | >= 9 | `npm --version` |
| CMake | >= 3.16 | `cmake --version` |
| C++ compiler | GCC 11+ or Clang 14+ | `g++ --version` |
| OpenSSL | dev headers | `dpkg -l libssl-dev` |
| libcurl | dev headers | `dpkg -l libcurl4-openssl-dev` |
| SQLite3 | dev headers | `dpkg -l libsqlite3-dev` |

### Install Build Dependencies (one-time)

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev libsqlite3-dev
```

**Fedora / RHEL:**
```bash
sudo dnf install cmake gcc-c++ libcurl-devel openssl-devel sqlite-devel
```

**Arch Linux:**
```bash
sudo pacman -S cmake gcc curl openssl sqlite
```

**openSUSE:**
```bash
sudo zypper install cmake gcc-c++ libcurl-devel libopenssl-devel sqlite3-devel
```

### Install CLOVE

```bash
npm install -g @cloveos/cli
```

This will:
1. Install the CLI binary (`clove`) to your PATH
2. Run `postinstall.js` which clones the repo and builds the C++ kernel
3. Place the kernel binary at `~/.clove/bin/clove_kernel`

If the postinstall build fails (missing deps), install the deps above and run:
```bash
clove build
```

### Verify

```bash
clove help       # should print full command list
clove status     # shows "not running" (expected before first start)
```

---

## All CLI Commands

### Kernel Lifecycle

| Command | Description |
|---------|-------------|
| `clove start` | Build kernel (if needed), start it, open dashboard |
| `clove stop` | Stop the kernel process |
| `clove status` / `clove s` | Show kernel health, agents, cost, uptime |
| `clove build` | Manually trigger kernel build from source |
| `clove dashboard` / `clove d` | Open dashboard in browser |

### Agent Runs

| Command | Description |
|---------|-------------|
| `clove run "goal" [--budget 0.50]` | Single agent run |
| `clove fleet "goal" [-n 3] [--budget 1.0]` | Parallel fleet run (SSE streaming) |

### Agent Management

| Command | Description |
|---------|-------------|
| `clove agent list` | List all persistent agents |
| `clove agent create <name>` | Create a new agent definition |
| `clove agent show <name>` | Show agent details |
| `clove agent enable <name>` | Enable an agent |
| `clove agent disable <name>` | Disable an agent |
| `clove agent run <name>` | Manually trigger an agent |
| `clove agent delete <name>` | Remove an agent |

### Connections (MCP Services)

| Command | Description |
|---------|-------------|
| `clove connect list` | Show all connections |
| `clove connect github [token]` | Connect GitHub via MCP |
| `clove connect slack [token]` | Connect Slack via MCP |
| `clove connect <service>` | Connect any supported service |
| `clove connect remove <name>` | Remove a connection |

**Supported services:** github, slack, filesystem, postgres, notion, google_drive

### Templates & Deploy

| Command | Description |
|---------|-------------|
| `clove templates` | List built-in + user templates |
| `clove create <name>` | Scaffold a new agent template |
| `clove deploy <name> --param key=value` | Deploy a template as a run |

### Worlds

| Command | Description |
|---------|-------------|
| `clove world list` | List all worlds |
| `clove world create <name>` | Create a new world |
| `clove world launch <template> --param key=value` | Launch a world template |

### MCP Server Management

| Command | Description |
|---------|-------------|
| `clove mcp list` | List MCP servers + tools |
| `clove mcp add <name> <command>` | Add an MCP server |

### Observability

| Command | Description |
|---------|-------------|
| `clove recall` / `clove memory` | Show shared memory blocks |
| `clove logs [N]` / `clove audit [N]` | Show N recent audit entries |
| `clove inference show` | Show inference gateway config |
| `clove inference set model <value>` | Set default model |
| `clove inference set max_cost <value>` | Set cost cap |

### Governance & Privacy

| Command | Description |
|---------|-------------|
| `clove policy` | Show policy recommendations |
| `clove privacy "text to scan"` | Scan text for PII |
| `clove replay status` | Check recording status |
| `clove replay start` | Start recording |
| `clove replay stop` | Stop recording |

### Configuration

| Command | Description |
|---------|-------------|
| `clove config` | Show full config JSON |
| `clove config set <key> <value>` | Set a config value |
| `clove config get <key>` | Get a config value |

**Config keys:** `kernelPath`, `apiPort`, `openrouterKey`, `privacy`, `privacyMode`, `sandbox`

### Scheduler

| Command | Description |
|---------|-------------|
| `clove scheduler` | Start the cron scheduler (runs in foreground) |

---

## Changes Made for Linux Support

### 1. CLI Source (`cli-js/src/cli.ts`)

**Bug fix: ESM `require()` call**
```diff
- try { require('fs').unlinkSync(PID_FILE) } catch {}
+ try { unlinkSync(PID_FILE) } catch {}
```
`require('fs')` crashes in ESM modules. Used the already-imported `unlinkSync`.

**Cross-platform URL opening**
```diff
- execSync(`open http://localhost:${cfg.apiPort}/dashboard`, { stdio: 'ignore' })
+ openUrl(`http://localhost:${cfg.apiPort}/dashboard`)
```
`open` is macOS-only. Added `openUrl()` that uses `xdg-open` on Linux, `open` on macOS.

**Platform-aware build instructions**
```diff
- console.log(c.dim('     cmake --build . -j$(sysctl -n hw.ncpu)'))
+ const cores = platform() === 'darwin' ? '$(sysctl -n hw.ncpu)' : '$(nproc)'
+ console.log(c.dim(`     cmake --build . -j${cores}`))
```
`sysctl -n hw.ncpu` doesn't exist on Linux. Shows `nproc` instead.

### 2. Kernel Build Fix (`libs/api/src/run_engine.cpp`)

```diff
- if (val) results += *val + "\n";
+ if (val) results += val->get<std::string>() + "\n";
```
GCC 13 rejects `nlohmann::json + const char[]`. Explicit `.get<std::string>()` fixes it.

### 3. Postinstall Script (`cli-js/scripts/postinstall.js`)

- Added Linux package manager detection (apt, dnf, pacman, zypper)
- Shows correct dependency install command per distro
- Platform-aware core count in build instructions (`nproc` vs `sysctl`)

### 4. Package Metadata (`cli-js/package.json`)

- `os: ["linux", "darwin"]` — declares Linux support
- `engines.node: ">=22.0.0"` — requires Node 22+ for ESM + `import.meta.dirname`
- `keywords` — added `cli`, `linux`
- `prepublishOnly` — ensures TypeScript builds before publish

---

## Build & Test Results (Linux)

### Environment

| Component | Version |
|-----------|---------|
| OS | Ubuntu (Linux 6.14.0-37-generic, x86_64) |
| Compiler | GCC 13.3.0 |
| CMake | 3.28 |
| Node.js | 22.x |
| OpenSSL | 3.0.13 |
| SQLite3 | 3.45.1 |
| libcurl | 8.5.0 |

### Unit Tests

```
165/165 tests passed, 0 tests failed
Total test time: 1.14 sec
```

### API Endpoints Verified

| Endpoint | Method | Status |
|----------|--------|--------|
| `/api/health` | GET | OK — v2.0.0, 86 syscalls |
| `/api/cost` | GET | OK — cost tracking active |
| `/api/memory` | POST | OK — created memory block |
| `/api/memory` | GET | OK — lists blocks |
| `/api/memory/search?q=` | GET | OK — relevance-scored search |
| `/api/sandbox/overview` | GET | OK — agent overview |
| `/api/history` | GET | OK — run history |
| `/api/schedules` | GET | OK — schedule listing |
| `/api/webhooks` | GET | OK — webhook listing |
| `/api/audit` | GET | OK — audit trail |
| `/api/exec` | POST | OK — permission enforcement |
| `/dashboard` | GET | OK — HTML dashboard served |

### CLI Commands Verified

| Command | Status |
|---------|--------|
| `clove help` | OK |
| `clove status` | OK |
| `clove recall` | OK |
| `clove logs` | OK |
| `clove config` | OK |
| `clove templates` | OK |

---

## npm Package Details

```
Package:   @cloveos/cli@0.2.0
Binary:    clove (-> dist/cli.js)
Engines:   node >= 22.0.0
Platforms: linux, darwin
Files:     dist/cli.js, dist/cli.d.ts, scripts/postinstall.js, package.json
```

---

## Publishing (for maintainers)

**v0.2.0 published on 2026-03-31** by `mukulkatewa`.

### Publishing a New Version

```bash
cd cli-js

# 1. Bump version in package.json
npm version patch   # or minor/major

# 2. Login (needs @cloveos org membership)
npm login

# 3. Dry run
npm publish --access public --dry-run

# 4. Publish
npm publish --access public
```

> `--access public` is required for scoped packages (`@cloveos/*`).
> If 2FA blocks publish, create a **Classic Automation Token** at https://www.npmjs.com/settings/YOUR_USER/tokens
> and set it with `npm config set //registry.npmjs.org/:_authToken=TOKEN`

### Org Members

| User | Role |
|------|------|
| aniiiixd | owner |
| mukulkatewa | developer |

### How Users Install

```bash
npm install -g @cloveos/cli
clove start
```

### Postinstall Flow

```
npm install -g @cloveos/cli
  |
  v
postinstall.js runs
  |
  +-- Kernel binary exists at ~/.clove/bin/clove_kernel?
  |     YES -> skip build, done
  |     NO  -> continue
  |
  +-- Check cmake, g++
  |     MISSING -> print distro-specific install command, exit
  |     OK      -> continue
  |
  +-- Source in package dir? (local install / dev)
  |     YES -> build from local source
  |     NO  -> git clone --depth 1 --branch v2 https://github.com/aniiiiXD/Clove.git
  |
  +-- cmake + make
  |
  +-- Copy binary to ~/.clove/bin/clove_kernel
  |
  +-- Done. "Run: clove start"
```

---

## File Directory Structure

```
cli-js/
  package.json              # npm package config
  tsconfig.json             # TypeScript config
  src/
    cli.ts                  # Main CLI source (all commands)
  dist/
    cli.js                  # Compiled JS (built by tsc)
    cli.d.ts                # Type declarations
  scripts/
    postinstall.js          # Auto-build kernel on npm install
```

---

## Files Changed (this branch)

```
cli-js/package.json          — scoped name, Linux keywords, metadata
cli-js/src/cli.ts            — ESM fix, cross-platform URL open, Linux build cmds
cli-js/scripts/postinstall.js — multi-distro package manager detection
libs/api/src/run_engine.cpp  — nlohmann::json type fix for GCC 13
docs/NPM_PUBLISH_LINUX.md   — this file
```
