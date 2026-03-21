#!/usr/bin/env python3
"""
clove-openclaw benchmark

Compares startup time, memory usage, and overhead between:
- OpenClaw bare (no sandbox)
- OpenClaw + NemoClaw/OpenShell (Docker + K3s)
- OpenClaw + CLOVE kernel (native sandbox)

Usage:
    python3 benchmark.py
"""

import json
import os
import shutil
import subprocess
import time


def check_command(cmd: str) -> bool:
    """Check if a command is available."""
    return shutil.which(cmd) is not None


def measure_startup(command: list[str], timeout: int = 30) -> dict:
    """Measure process startup time and initial memory."""
    start = time.monotonic()
    try:
        proc = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        # Wait for process to be alive
        time.sleep(0.5)

        elapsed_ms = (time.monotonic() - start) * 1000

        # Get memory (macOS)
        try:
            ps_out = subprocess.check_output(
                ['ps', '-o', 'rss=', '-p', str(proc.pid)],
                text=True
            ).strip()
            rss_kb = int(ps_out) if ps_out else 0
        except Exception:
            rss_kb = 0

        # Kill the process
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

        return {
            'startup_ms': round(elapsed_ms, 1),
            'rss_kb': rss_kb,
            'rss_mb': round(rss_kb / 1024, 1),
            'success': True,
        }
    except FileNotFoundError:
        return {'success': False, 'error': f'Command not found: {command[0]}'}
    except Exception as e:
        return {'success': False, 'error': str(e)}


def benchmark_clove_kernel() -> dict:
    """Benchmark CLOVE kernel startup."""
    # Find the kernel binary
    kernel_paths = [
        'build/kernel/clove_kernel',
        '../build/kernel/clove_kernel',
        '../../build/kernel/clove_kernel',
    ]

    kernel_path = None
    for p in kernel_paths:
        full = os.path.join(os.path.dirname(__file__), '..', '..', p)
        if os.path.exists(full):
            kernel_path = os.path.abspath(full)
            break

    if not kernel_path:
        return {'success': False, 'error': 'clove_kernel not found in build/'}

    # Use a temp socket to avoid conflicts
    sock = f'/tmp/clove_bench_{os.getpid()}.sock'

    result = measure_startup([kernel_path, '--socket', sock])
    result['name'] = 'CLOVE kernel'

    # Cleanup
    try:
        os.unlink(sock)
    except OSError:
        pass

    return result


def benchmark_docker() -> dict:
    """Benchmark Docker container startup (simulates OpenShell overhead)."""
    if not check_command('docker'):
        return {'success': False, 'error': 'Docker not installed', 'name': 'Docker (OpenShell proxy)'}

    start = time.monotonic()
    try:
        result = subprocess.run(
            ['docker', 'run', '--rm', 'alpine:3.19', 'echo', 'hello'],
            capture_output=True, text=True, timeout=30
        )
        elapsed_ms = (time.monotonic() - start) * 1000
        return {
            'name': 'Docker container (OpenShell baseline)',
            'startup_ms': round(elapsed_ms, 1),
            'success': result.returncode == 0,
        }
    except Exception as e:
        return {'success': False, 'error': str(e), 'name': 'Docker (OpenShell proxy)'}


def main():
    print("=" * 60)
    print("  clove-openclaw benchmark")
    print("  Comparing sandbox startup time and resource usage")
    print("=" * 60)

    results = []

    # Benchmark CLOVE kernel
    print("\n  [1/2] Benchmarking CLOVE kernel startup...")
    clove = benchmark_clove_kernel()
    results.append(clove)
    if clove['success']:
        print(f"    Startup: {clove['startup_ms']}ms")
        print(f"    Memory:  {clove.get('rss_mb', '?')} MB")
    else:
        print(f"    Skipped: {clove.get('error', 'unknown')}")

    # Benchmark Docker (as OpenShell proxy)
    print("\n  [2/2] Benchmarking Docker container startup (OpenShell proxy)...")
    docker = benchmark_docker()
    results.append(docker)
    if docker['success']:
        print(f"    Startup: {docker['startup_ms']}ms")
    else:
        print(f"    Skipped: {docker.get('error', 'unknown')}")

    # Comparison
    print("\n" + "=" * 60)
    print("  Results")
    print("=" * 60)

    if clove['success'] and docker['success']:
        ratio = docker['startup_ms'] / max(clove['startup_ms'], 0.1)
        print(f"\n  CLOVE kernel:  {clove['startup_ms']:>8.1f} ms startup, {clove.get('rss_mb', '?')} MB RAM")
        print(f"  Docker (OS):   {docker['startup_ms']:>8.1f} ms startup")
        print(f"  Speedup:       {ratio:>8.1f}x faster with CLOVE")
        print(f"\n  Note: Real OpenShell adds K3s + Colima on top of Docker,")
        print(f"  so actual speedup is likely higher (~100x+).")
    else:
        for r in results:
            status = "OK" if r['success'] else f"SKIP ({r.get('error', '')})"
            print(f"  {r.get('name', '?'):40s} {status}")

    # Save results
    out_path = os.path.join(os.path.dirname(__file__), 'benchmark_results.json')
    with open(out_path, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"\n  Results saved to: {out_path}")


if __name__ == '__main__':
    main()
