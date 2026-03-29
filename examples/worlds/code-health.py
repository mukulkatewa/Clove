#!/usr/bin/env python3
"""
Code Health World — first working CLOVE world example.

3 agents in an isolated world, each reviewing a project from a different angle.
They coordinate through shared state and produce a merged report.

Usage:
    # Make sure kernel is running
    curl http://localhost:8080/api/health

    # Run it
    python3 examples/worlds/code-health.py ./path/to/project

    # Or point it at CLOVE itself
    python3 examples/worlds/code-health.py .
"""

import sys
import json
import time
import requests
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

API = "http://localhost:8080"


def api(method, path, body=None):
    """Call the kernel API."""
    r = requests.request(method, f"{API}{path}", json=body, timeout=60)
    return r.json()


def create_world(name):
    """Create an isolated world."""
    result = api("POST", "/api/worlds", {"name": name})
    print(f"  World created: {name} (id: {result.get('id', '?')})")
    return result


def run_agent(name, goal, budget=0.30, tools=None):
    """Run a single agent via the kernel."""
    body = {
        "goal": goal,
        "budget": budget,
        "max_steps": 10,
        "agent_name": name,
    }
    if tools:
        body["tools"] = tools

    print(f"  [{name}] starting...")
    start = time.time()
    result = api("POST", "/api/run", body)
    elapsed = time.time() - start

    success = result.get("success", False)
    cost = result.get("total_cost_usd", 0)
    steps = result.get("steps", 0)
    content = result.get("content", "")

    status = "OK" if success else "FAIL"
    print(f"  [{name}] {status} — {steps} steps, ${cost:.4f}, {elapsed:.1f}s")

    return {
        "name": name,
        "success": success,
        "content": content,
        "cost": cost,
        "steps": steps,
        "elapsed": elapsed,
    }


def store(key, value):
    """Store a value in the kernel state store."""
    api("POST", "/api/store", {"key": key, "value": value})


def fetch(key):
    """Fetch a value from the kernel state store."""
    r = api("GET", f"/api/store/{key}")
    return r.get("value")


def main():
    project_path = sys.argv[1] if len(sys.argv) > 1 else "."
    project_path = str(Path(project_path).resolve())

    print()
    print("  CODE HEALTH WORLD")
    print("  ─────────────────────────────────────")
    print(f"  Project: {project_path}")
    print()

    # Check kernel
    try:
        health = api("GET", "/api/health")
        print(f"  Kernel: v{health['version']} ({health['syscall_count']} syscalls)")
    except Exception:
        print("  ERROR: Kernel not running. Start it with: clove start")
        sys.exit(1)

    # Create the world
    world_name = f"code-health-{int(time.time()) % 10000}"
    create_world(world_name)
    print()

    # Store the project path in shared state so agents can reference it
    store(f"{world_name}:project_path", project_path)

    # Define the 3 agents
    # First, discover what files exist
    file_list = ""
    try:
        import subprocess
        result = subprocess.run(
            ["find", project_path, "-type", "f", "-name", "*.ts", "-o", "-name", "*.py", "-o", "-name", "*.js", "-o", "-name", "*.tsx", "-o", "-name", "*.cpp", "-o", "-name", "*.hpp"],
            capture_output=True, text=True, timeout=5
        )
        files = [f for f in result.stdout.strip().split('\n') if f and 'node_modules' not in f and '.next' not in f][:20]
        file_list = "\n".join(files)
    except Exception:
        file_list = f"Run: find {project_path} -type f to discover files"

    agents = [
        {
            "name": "security-scanner",
            "goal": f"""You have access to read_file and exec tools. Review these files for security issues:

{file_list}

Use read_file to read each file. Focus on: hardcoded secrets, API keys, SQL injection, XSS, insecure auth patterns.
For each issue found: state the file path, describe the issue, rate severity (critical/warning/info), suggest a fix.
Only report real issues you actually find in the code, not hypothetical ones. Be concise.""",
            "tools": ["read_file", "exec"],
        },
        {
            "name": "dependency-checker",
            "goal": f"""You have access to read_file and exec tools. Check dependencies for the project at {project_path}.

Use read_file to read dependency files like:
- {project_path}/package.json
- {project_path}/requirements.txt
- {project_path}/CMakeLists.txt
- {project_path}/Cargo.toml

Use exec to run commands like 'cat {project_path}/package.json' if read_file has issues.
Check for: outdated packages, unnecessary deps, version pinning issues.
List each finding: package name, issue, recommendation. Be concise and factual.""",
            "tools": ["read_file", "exec"],
        },
        {
            "name": "code-quality-reviewer",
            "goal": f"""You have access to read_file and exec tools. Review code quality for these files:

{file_list}

Use read_file to read the files. Focus on: dead code, duplicated logic, overly complex functions, missing error handling, poor naming conventions.
List the top 5-10 most impactful issues: file path, description, improvement suggestion. Be concise.""",
            "tools": ["read_file", "exec"],
        },
    ]

    # Run all 3 agents in parallel
    print("  Running 3 agents in parallel...")
    print("  ─────────────────────────────────────")
    print()

    results = []
    total_cost = 0
    total_start = time.time()

    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = {
            pool.submit(run_agent, a["name"], a["goal"], 0.30, a["tools"]): a
            for a in agents
        }
        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            total_cost += result["cost"]

            # Store result in shared state
            store(f"{world_name}:{result['name']}", result["content"])

    total_elapsed = time.time() - total_start
    print()

    # Synthesis: run a 4th agent that reads all findings and merges them
    print("  Synthesizing findings...")
    print()

    findings = []
    for r in results:
        findings.append(f"=== {r['name'].upper()} ===\n{r['content']}")

    synthesis_goal = f"""You are given security, dependency, and code quality reports for a project.
Merge them into a single, structured health report with these sections:
1. Overall Health Score (A/B/C/D/F with one-line justification)
2. Critical Issues (must fix immediately)
3. Warnings (should fix soon)
4. Recommendations (nice to have)
5. Summary (2-3 sentences)

Reports:
{chr(10).join(findings)}"""

    synthesis = run_agent("synthesizer", synthesis_goal, 0.20, ["write_file"])
    total_cost += synthesis["cost"]

    # Store final report
    store(f"{world_name}:report", synthesis["content"])

    print()
    print("  ═══════════════════════════════════════")
    print("  REPORT")
    print("  ═══════════════════════════════════════")
    print()
    print(synthesis["content"])
    print()
    print("  ─────────────────────────────────────")
    print(f"  World:    {world_name}")
    print(f"  Agents:   {len(results) + 1}")
    print(f"  Cost:     ${total_cost:.4f}")
    print(f"  Time:     {total_elapsed:.1f}s (parallel) + {synthesis['elapsed']:.1f}s (synthesis)")
    print(f"  Results stored in kernel state under '{world_name}:*'")
    print("  ─────────────────────────────────────")
    print()


if __name__ == "__main__":
    main()
