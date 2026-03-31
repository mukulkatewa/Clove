#!/usr/bin/env python3
"""
Code Intelligence Mapper — maps a codebase into persistent CLOVE memory.

Runs 3 agents that analyze different aspects of the project, then a
synthesizer merges findings into a CORE memory block. Subsequent agent
runs can recall this architecture without re-exploring.

Usage:
    python3 examples/worlds/code-mapper.py ./path/to/project

The architecture map is stored in kernel memory as 'codebase-architecture'
and can be recalled by any agent via the 'recall' tool.
"""

import sys
import json
import time
import subprocess
import requests
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

API = "http://localhost:8080"


def api(method, path, body=None):
    r = requests.request(method, f"{API}{path}", json=body, timeout=120)
    return r.json()


def run_agent(name, goal, budget=0.25, tools=None):
    print(f"  [{name}] starting...")
    start = time.time()
    result = api("POST", "/api/run", {
        "goal": goal, "budget": budget, "max_steps": 10,
        "agent_name": name, "tools": tools or ["exec", "read_file", "remember"],
    })
    elapsed = time.time() - start
    status = "OK" if result.get("success") else "FAIL"
    cost = result.get("total_cost_usd", 0)
    print(f"  [{name}] {status} — ${cost:.4f}, {elapsed:.1f}s")
    return result


def main():
    project = sys.argv[1] if len(sys.argv) > 1 else "."
    project = str(Path(project).resolve())

    print()
    print("  CODE INTELLIGENCE MAPPER")
    print("  ─────────────────────────────────────")
    print(f"  Project: {project}")
    print()

    # Check kernel
    try:
        health = api("GET", "/api/health")
        print(f"  Kernel: v{health['version']} ({health['syscall_count']} syscalls)")
    except Exception:
        print("  ERROR: Kernel not running. Start with: clove start")
        sys.exit(1)

    # Discover files
    try:
        result = subprocess.run(
            ["find", project, "-type", "f",
             "-name", "*.ts", "-o", "-name", "*.py", "-o", "-name", "*.js",
             "-o", "-name", "*.tsx", "-o", "-name", "*.go", "-o", "-name", "*.rs",
             "-o", "-name", "*.cpp", "-o", "-name", "*.hpp"],
            capture_output=True, text=True, timeout=5
        )
        files = [f for f in result.stdout.strip().split('\n')
                 if f and 'node_modules' not in f and '.next' not in f and 'dist/' not in f][:30]
        file_list = "\n".join(files)
        lang_counts = {}
        for f in files:
            ext = f.rsplit('.', 1)[-1] if '.' in f else 'unknown'
            lang_counts[ext] = lang_counts.get(ext, 0) + 1
        langs = ", ".join(f"{ext}: {count}" for ext, count in sorted(lang_counts.items(), key=lambda x: -x[1]))
    except Exception:
        file_list = f"(run: find {project} -type f)"
        langs = "unknown"

    print(f"  Files: {len(files)} source files ({langs})")
    print()

    # Create world
    world_name = f"code-intel-{int(time.time()) % 10000}"
    api("POST", "/api/worlds", {"name": world_name})
    print(f"  World: {world_name}")
    print()

    # Run 3 mapper agents in parallel
    agents = [
        {
            "name": "structure-mapper",
            "goal": f"""Map the project structure at {project}.

Files found:
{file_list}

Languages: {langs}

Use exec and read_file to understand:
1. Directory structure and what each directory contains
2. Entry points (main files, index files, app files)
3. Configuration files (package.json, tsconfig, Cargo.toml, etc.)
4. Build system and scripts

Use the 'remember' tool to store your findings as a memory block named 'project-structure'.
Format: bullet points, concise, factual.""",
        },
        {
            "name": "api-mapper",
            "goal": f"""Find all public APIs and exports in the project at {project}.

Key files:
{file_list}

Use exec to search for patterns:
- grep -rn "export " or "module.exports" for JS/TS
- grep -rn "def " or "class " for Python
- grep -rn "pub fn" or "pub struct" for Rust
- grep -rn "func " for Go

Use read_file on the most important files.

Use the 'remember' tool to store findings as a memory block named 'project-apis'.
Format: list each API/export with file path and brief description.""",
            "tools": ["exec", "read_file", "remember"],
        },
        {
            "name": "dependency-mapper",
            "goal": f"""Map the internal dependency graph of the project at {project}.

Key files:
{file_list}

Use exec to find import patterns:
- grep -rn "import " or "require(" for JS/TS
- grep -rn "from .* import" for Python
- grep -rn "use " for Rust

Identify:
1. Which modules import which other modules
2. Shared utilities and common dependencies
3. External package dependencies (from package.json, requirements.txt, etc.)
4. Circular or unusual dependency patterns

Use the 'remember' tool to store findings as a memory block named 'project-dependencies'.
Format: dependency list with directions (A imports B).""",
            "tools": ["exec", "read_file", "remember"],
        },
    ]

    print("  Running 3 mapper agents in parallel...")
    print("  ─────────────────────────────────────")
    print()

    results = []
    total_cost = 0
    total_start = time.time()

    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = {
            pool.submit(run_agent, a["name"], a["goal"], 0.25, a.get("tools")): a
            for a in agents
        }
        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            total_cost += result.get("total_cost_usd", 0)

    total_elapsed = time.time() - total_start
    print()

    # Synthesis: merge all findings into architecture doc + CORE memory
    findings = []
    for r in results:
        content = r.get("content", "")
        if content:
            findings.append(content[:2000])

    print("  Synthesizing architecture map...")
    print()

    synthesis = run_agent("architect", f"""You have three reports about a codebase:

=== STRUCTURE ===
{findings[0] if len(findings) > 0 else '(none)'}

=== APIs ===
{findings[1] if len(findings) > 1 else '(none)'}

=== DEPENDENCIES ===
{findings[2] if len(findings) > 2 else '(none)'}

Merge these into a single, comprehensive architecture document:
1. Project overview (1-2 sentences)
2. Directory structure with purpose of each directory
3. Key modules and their public APIs
4. Dependency graph (what imports what)
5. Entry points and build process
6. Notable patterns or concerns

IMPORTANT: Use the 'remember' tool to store this document as a CORE memory block named 'codebase-architecture'.
This memory will persist and be available to all future agents working on this project.""",
        budget=0.20,
        tools=["remember", "recall"],
    )

    total_cost += synthesis.get("total_cost_usd", 0)

    print()
    print("  ═══════════════════════════════════════")
    print("  ARCHITECTURE MAP STORED")
    print("  ═══════════════════════════════════════")
    print()
    print(synthesis.get("content", "")[:1000])
    print()
    print("  ─────────────────────────────────────")
    print(f"  World:     {world_name}")
    print(f"  Agents:    4 (3 mappers + 1 architect)")
    print(f"  Cost:      ${total_cost:.4f}")
    print(f"  Time:      {total_elapsed:.1f}s (parallel) + {time.time() - total_start - total_elapsed:.1f}s (synthesis)")
    print(f"  Memory:    project-structure, project-apis, project-dependencies, codebase-architecture")
    print()
    print("  Any future agent can now recall this with:")
    print("    recall('codebase architecture')")
    print("    recall('project APIs')")
    print("    recall('project dependencies')")
    print()


if __name__ == "__main__":
    main()
