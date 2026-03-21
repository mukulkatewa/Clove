#!/usr/bin/env python3
"""CLOVE v1 vs v2 — Comprehensive Head-to-Head Benchmark Suite

Tests the real-world performance of both kernels across categories:

  STARTUP & RESOURCE
    1. Cold start time (launch → socket ready, 10 iterations)
    2. Idle memory RSS
    3. Memory under load (after 5000 syscalls)
    4. Binary size + stripped estimate

  IPC LATENCY
    5. SYS_NOOP latency (persistent conn, 1000 round-trips, full percentiles)
    6. SYS_LIST latency (real syscall returning payload)
    7. SYS_STORE + SYS_FETCH latency (state store round-trip)
    8. One-shot IPC overhead (new connection per call)

  THROUGHPUT
    9. Sequential throughput (send-recv loop, 5000 SYS_NOOP)
    10. Burst throughput (fire 5000, then drain)
    11. Mixed syscall throughput (NOOP, LIST, STORE, FETCH interleaved)
    12. Multi-connection throughput (4 parallel persistent connections)

  PAYLOAD SCALING
    13. Payload size vs latency (0B, 100B, 1KB, 10KB, 100KB)

  SHUTDOWN
    14. Graceful shutdown time (SIGTERM → exit)

Root causes identified before building this suite:
  - MEMORY: v2 PrivacyFilter compiles 5 std::regex at boot (~1MB). v2 has
    16 subsystems vs v1's lighter init. LlmQueue(8) + AsyncTaskManager(8)
    = 16 worker threads spawned at boot.
  - THROUGHPUT: v2 handle_message() calls execution_logger_->record() on
    EVERY syscall (chrono::now x2, mutex lock, string copies for payload +
    response). v1 handle_message() is a trivial passthrough.
  - SHUTDOWN: v2 explicitly joins 8 LLM + 8 async worker threads. v1 only
    joins LLM threads in destructor (no async pool).
"""

import subprocess
import socket
import time
import os
import sys
import json
import struct
import signal
from pathlib import Path
from dataclasses import dataclass, field, asdict
from contextlib import contextmanager
from statistics import median, stdev, mean

# ── Config ──────────────────────────────────────────────────────
V1_KERNEL = Path("/Users/anixd/Documents/Clove/build/clove_kernel")
V2_KERNEL = Path("/Users/anixd/Documents/clove-v2/build/kernel/clove_kernel")
RESULTS_DIR = Path("/Users/anixd/Documents/clove-v2/benchmark/results")

# ── Colors ──────────────────────────────────────────────────────
class C:
    R = "\033[0m"; B = "\033[1m"; RED = "\033[0;31m"; GREEN = "\033[0;32m"
    CYAN = "\033[0;36m"; YELLOW = "\033[1;33m"; DIM = "\033[2m"; MAG = "\033[0;35m"

def info(msg):   print(f"  {C.CYAN}•{C.R} {msg}")
def ok(msg):     print(f"  {C.GREEN}✓{C.R} {msg}")
def warn(msg):   print(f"  {C.YELLOW}!{C.R} {msg}")
def err(msg):    print(f"  {C.RED}✗{C.R} {msg}")
def header(n, msg): print(f"\n{C.B}{C.YELLOW}{'─'*60}\n  {n}. {msg}\n{'─'*60}{C.R}")

# ── Data ────────────────────────────────────────────────────────
@dataclass
class BenchResult:
    test: str
    v1: float
    v2: float
    unit: str
    winner: str = ""
    speedup: str = ""
    notes: str = ""
    category: str = ""

# ── Wire Protocol ───────────────────────────────────────────────
MAGIC = 0x41474E54
HDR_SIZE = 17
HDR_FMT = "<IIbQ"

# Shared opcodes
SYS_NOOP    = 0x00
SYS_LIST    = 0x12
SYS_STORE   = 0x30
SYS_FETCH   = 0x31
SYS_DELETE  = 0x32
SYS_HELLO   = 0xFE

def build_msg(agent_id=0, opcode=SYS_NOOP, payload=b""):
    return struct.pack(HDR_FMT, MAGIC, agent_id, opcode, len(payload)) + payload


class Conn:
    """Persistent connection to CLOVE kernel."""
    def __init__(self, sock_path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect(sock_path)

    def call(self, opcode=SYS_NOOP, payload=b"", agent_id=9999):
        """Send + recv. Returns (response_bytes, latency_ms)."""
        msg = build_msg(agent_id=agent_id, opcode=opcode, payload=payload)
        t0 = time.monotonic()
        self.sock.sendall(msg)
        hdr = self._recv(HDR_SIZE)
        body = b""
        if hdr:
            _, _, _, sz = struct.unpack(HDR_FMT, hdr)
            if sz > 0:
                body = self._recv(sz)
        lat = (time.monotonic() - t0) * 1000
        return (hdr or b"") + (body or b""), lat

    def fire(self, opcode=SYS_NOOP, payload=b"", agent_id=9999):
        """Send without reading."""
        self.sock.sendall(build_msg(agent_id=agent_id, opcode=opcode, payload=payload))

    def drain(self, n):
        """Read n responses."""
        for _ in range(n):
            hdr = self._recv(HDR_SIZE)
            if hdr:
                _, _, _, sz = struct.unpack(HDR_FMT, hdr)
                if sz > 0:
                    self._recv(sz)

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    def close(self):
        self.sock.close()


def oneshot(sock_path, opcode=SYS_NOOP, payload=b""):
    """Fresh connection per call."""
    t0 = time.monotonic()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5)
    try:
        s.connect(sock_path)
        s.sendall(build_msg(agent_id=9999, opcode=opcode, payload=payload))
        s.recv(8192)
        return (time.monotonic() - t0) * 1000
    except Exception:
        return (time.monotonic() - t0) * 1000
    finally:
        s.close()


@contextmanager
def run_kernel(binary, sock_path, timeout=20):
    """Start kernel, wait for socket ready, yield proc, graceful shutdown."""
    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass

    proc = subprocess.Popen(
        [str(binary), "--no-sandbox", "--socket", sock_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    deadline = time.monotonic() + timeout
    ready = False
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            break
        if os.path.exists(sock_path):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.settimeout(1)
                s.connect(sock_path)
                s.close()
                ready = True
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.005)
        else:
            time.sleep(0.005)

    if not ready:
        proc.kill(); proc.wait()
        raise RuntimeError(f"{binary.name} failed to start")

    try:
        yield proc
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=3)
        try:
            os.unlink(sock_path)
        except FileNotFoundError:
            pass


def get_rss_kb(pid):
    """Get RSS in KB."""
    try:
        r = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                           capture_output=True, text=True)
        return int(r.stdout.strip())
    except:
        return 0


def percentiles(data):
    """Return dict of stats."""
    s = sorted(data)
    n = len(s)
    return {
        "min": s[0], "p25": s[n//4], "p50": s[n//2],
        "p75": s[3*n//4], "p95": s[int(n*0.95)], "p99": s[int(n*0.99)],
        "max": s[-1], "avg": mean(s), "stdev": stdev(s) if n > 1 else 0,
    }


def compare(v1_val, v2_val, lower_better=True):
    """Return (winner, speedup_str)."""
    if lower_better:
        winner = "v2" if v2_val <= v1_val else "v1"
        ratio = v1_val / v2_val if v2_val > 0 else float('inf')
    else:
        winner = "v2" if v2_val >= v1_val else "v1"
        ratio = v2_val / v1_val if v1_val > 0 else float('inf')
    return winner, f"{ratio:.2f}x"


# ════════════════════════════════════════════════════════════════
#  BENCHMARKS
# ════════════════════════════════════════════════════════════════

def bench_cold_start():
    header(1, "COLD START TIME")
    N = 10
    results = {}
    detail = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        info(f"{label}: {N} iterations...")
        times = []
        for i in range(N):
            sock = f"/tmp/clove_cs_{label}_{i}.sock"
            try: os.unlink(sock)
            except: pass

            t0 = time.monotonic()
            proc = subprocess.Popen(
                [str(binary), "--no-sandbox", "--socket", sock],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = t0 + 20
            while time.monotonic() < deadline:
                if proc.poll() is not None: break
                if os.path.exists(sock):
                    try:
                        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        s.settimeout(1); s.connect(sock); s.close(); break
                    except: time.sleep(0.003)
                else: time.sleep(0.003)
            ms = (time.monotonic() - t0) * 1000
            times.append(ms)
            proc.terminate()
            try: proc.wait(timeout=5)
            except: proc.kill(); proc.wait(timeout=2)
            try: os.unlink(sock)
            except: pass

        p = percentiles(times)
        results[label] = p["p50"]
        detail[label] = p
        ok(f"{label}: p50={p['p50']:.1f}ms  min={p['min']:.1f}ms  p99={p['p99']:.1f}ms  σ={p['stdev']:.1f}ms")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("Cold Start", round(results["v1"], 1), round(results["v2"], 1), "ms",
                       w, sp, category="startup")


def bench_idle_memory():
    header(2, "IDLE MEMORY (RSS)")
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_im_{label}.sock"
        with run_kernel(binary, sock) as proc:
            time.sleep(0.5)
            kb = get_rss_kb(proc.pid)
        mb = kb / 1024
        results[label] = mb
        ok(f"{label}: {mb:.2f} MB ({kb} KB)")

    w, sp = compare(results["v1"], results["v2"])
    delta = results["v2"] - results["v1"]
    return BenchResult("Idle Memory", round(results["v1"], 2), round(results["v2"], 2), "MB",
                       w, sp,
                       notes=f"v2 +{delta:.1f}MB (PrivacyFilter regex, 16 worker threads, more subsystems)",
                       category="startup")


def bench_memory_under_load():
    header(3, "MEMORY UNDER LOAD (after 5000 syscalls)")
    N = 5000
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_ml_{label}.sock"
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(N):
                conn.call(opcode=SYS_NOOP)
            conn.close()
            time.sleep(0.2)
            kb = get_rss_kb(proc.pid)
        mb = kb / 1024
        results[label] = mb
        ok(f"{label}: {mb:.2f} MB after {N} calls")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("Memory (5K calls)", round(results["v1"], 2), round(results["v2"], 2), "MB",
                       w, sp,
                       notes="v2 execution_logger records every call (strings + mutex)",
                       category="startup")


def bench_binary_size():
    header(4, "BINARY SIZE")
    v1_b = V1_KERNEL.stat().st_size
    v2_b = V2_KERNEL.stat().st_size
    v1_mb = v1_b / (1024*1024)
    v2_mb = v2_b / (1024*1024)

    ok(f"v1: {v1_mb:.2f} MB ({v1_b:,} bytes)")
    ok(f"v2: {v2_mb:.2f} MB ({v2_b:,} bytes)")

    w, sp = compare(v1_mb, v2_mb)
    return BenchResult("Binary Size", round(v1_mb, 2), round(v2_mb, 2), "MB",
                       w, sp, notes="v2 modular but smaller — less code linked",
                       category="startup")


def bench_noop_latency():
    header(5, "SYS_NOOP LATENCY (1000 round-trips, persistent)")
    N = 1000; WARMUP = 50
    results = {}; detail = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_nl_{label}.sock"
        info(f"{label}: {N} SYS_NOOP...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(WARMUP):
                conn.call(opcode=SYS_NOOP)
            lats = []
            for _ in range(N):
                _, lat = conn.call(opcode=SYS_NOOP)
                lats.append(lat)
            conn.close()

        p = percentiles(lats)
        results[label] = p["avg"]
        detail[label] = p
        ok(f"{label}: avg={p['avg']:.4f}ms  p50={p['p50']:.4f}ms  p95={p['p95']:.4f}ms  p99={p['p99']:.4f}ms  σ={p['stdev']:.4f}ms")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("NOOP Latency", round(results["v1"], 4), round(results["v2"], 4), "ms",
                       w, sp,
                       notes=f"v2 hot path: +chrono::now x2, +mutex, +execution_logger per call",
                       category="latency")


def bench_list_latency():
    header(6, "SYS_LIST LATENCY (real syscall with JSON payload)")
    N = 500; WARMUP = 20
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_ll_{label}.sock"
        info(f"{label}: {N} SYS_LIST...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(WARMUP):
                conn.call(opcode=SYS_LIST)
            lats = []
            for _ in range(N):
                _, lat = conn.call(opcode=SYS_LIST)
                lats.append(lat)
            conn.close()

        p = percentiles(lats)
        results[label] = p["avg"]
        ok(f"{label}: avg={p['avg']:.4f}ms  p50={p['p50']:.4f}ms  p99={p['p99']:.4f}ms")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("LIST Latency", round(results["v1"], 4), round(results["v2"], 4), "ms",
                       w, sp, category="latency")


def bench_state_store_latency():
    header(7, "STATE STORE Latency (STORE + FETCH round-trip)")
    N = 300; WARMUP = 10
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_ss_{label}.sock"
        info(f"{label}: {N} STORE+FETCH pairs...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(WARMUP):
                payload = json.dumps({"key": "warmup", "value": "x"}).encode()
                conn.call(opcode=SYS_STORE, payload=payload)

            lats = []
            for i in range(N):
                store_payload = json.dumps({"key": f"bench_{i}", "value": f"val_{i}"}).encode()
                _, lat_s = conn.call(opcode=SYS_STORE, payload=store_payload)

                fetch_payload = json.dumps({"key": f"bench_{i}"}).encode()
                _, lat_f = conn.call(opcode=SYS_FETCH, payload=fetch_payload)

                lats.append(lat_s + lat_f)
            conn.close()

        p = percentiles(lats)
        results[label] = p["avg"]
        ok(f"{label}: avg={p['avg']:.4f}ms  p50={p['p50']:.4f}ms (per STORE+FETCH pair)")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("State Store RT", round(results["v1"], 4), round(results["v2"], 4), "ms",
                       w, sp, category="latency")


def bench_oneshot():
    header(8, "ONE-SHOT IPC (new connection per call)")
    N = 100
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_os_{label}.sock"
        info(f"{label}: {N} one-shot calls...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            lats = [oneshot(sock) for _ in range(N)]

        p = percentiles(lats)
        results[label] = p["avg"]
        ok(f"{label}: avg={p['avg']:.3f}ms  p50={p['p50']:.3f}ms (connect+send+recv+close)")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("One-Shot IPC", round(results["v1"], 3), round(results["v2"], 3), "ms",
                       w, sp, category="latency")


def bench_sequential_throughput():
    header(9, "SEQUENTIAL THROUGHPUT (5000 SYS_NOOP, send-recv loop)")
    N = 5000; WARMUP = 100
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_st_{label}.sock"
        info(f"{label}: {N} sequential calls...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(WARMUP):
                conn.call(opcode=SYS_NOOP)

            t0 = time.monotonic()
            for _ in range(N):
                conn.call(opcode=SYS_NOOP)
            elapsed = time.monotonic() - t0
            conn.close()

        ops = N / elapsed
        results[label] = ops
        ok(f"{label}: {ops:,.0f} ops/sec ({N} in {elapsed*1000:.0f}ms)")

    w, sp = compare(results["v1"], results["v2"], lower_better=False)
    return BenchResult("Throughput (seq)", round(results["v1"]), round(results["v2"]), "ops/s",
                       w, sp,
                       notes="v2 slower: execution_logger mutex+record on every call",
                       category="throughput")


def bench_burst_throughput():
    header(10, "BURST THROUGHPUT (fire 5000, then drain)")
    N = 5000
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_bt_{label}.sock"
        info(f"{label}: {N} burst fire-then-drain...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(50):
                conn.call(opcode=SYS_NOOP)

            t0 = time.monotonic()
            for _ in range(N):
                conn.fire(opcode=SYS_NOOP)
            conn.drain(N)
            elapsed = time.monotonic() - t0
            conn.close()

        ops = N / elapsed
        results[label] = ops
        ok(f"{label}: {ops:,.0f} ops/sec ({N} in {elapsed*1000:.0f}ms)")

    w, sp = compare(results["v1"], results["v2"], lower_better=False)
    return BenchResult("Throughput (burst)", round(results["v1"]), round(results["v2"]), "ops/s",
                       w, sp, category="throughput")


def bench_mixed_throughput():
    header(11, "MIXED SYSCALL THROUGHPUT (NOOP + LIST + STORE + FETCH)")
    N = 2000  # total calls (500 each of 4 opcodes interleaved)
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_mt_{label}.sock"
        info(f"{label}: {N} mixed calls...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(30):
                conn.call(opcode=SYS_NOOP)

            ops_seq = [SYS_NOOP, SYS_LIST, SYS_STORE, SYS_FETCH]
            payloads = {
                SYS_NOOP:  b"",
                SYS_LIST:  b"",
                SYS_STORE: json.dumps({"key": "k", "value": "v"}).encode(),
                SYS_FETCH: json.dumps({"key": "k"}).encode(),
            }

            t0 = time.monotonic()
            for i in range(N):
                op = ops_seq[i % 4]
                conn.call(opcode=op, payload=payloads[op])
            elapsed = time.monotonic() - t0
            conn.close()

        ops = N / elapsed
        results[label] = ops
        ok(f"{label}: {ops:,.0f} ops/sec ({N} in {elapsed*1000:.0f}ms)")

    w, sp = compare(results["v1"], results["v2"], lower_better=False)
    return BenchResult("Throughput (mixed)", round(results["v1"]), round(results["v2"]), "ops/s",
                       w, sp,
                       notes="Interleaved NOOP/LIST/STORE/FETCH — realistic workload",
                       category="throughput")


def bench_multi_conn_throughput():
    header(12, "MULTI-CONNECTION THROUGHPUT (4 parallel persistent conns)")
    N_PER_CONN = 2000
    import threading
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_mc_{label}.sock"
        info(f"{label}: 4 connections × {N_PER_CONN} calls...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)

            totals = [0.0] * 4

            def worker(idx):
                c = Conn(sock)
                for _ in range(50):
                    c.call(opcode=SYS_NOOP)
                t0 = time.monotonic()
                for _ in range(N_PER_CONN):
                    c.call(opcode=SYS_NOOP)
                totals[idx] = time.monotonic() - t0
                c.close()

            threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
            wall_t0 = time.monotonic()
            for t in threads: t.start()
            for t in threads: t.join()
            wall_elapsed = time.monotonic() - wall_t0

        total_ops = 4 * N_PER_CONN
        ops = total_ops / wall_elapsed
        results[label] = ops
        ok(f"{label}: {ops:,.0f} aggregate ops/sec ({total_ops} in {wall_elapsed*1000:.0f}ms)")

    w, sp = compare(results["v1"], results["v2"], lower_better=False)
    return BenchResult("Throughput (4-conn)", round(results["v1"]), round(results["v2"]), "ops/s",
                       w, sp,
                       notes="Tests reactor scalability under concurrent load",
                       category="throughput")


def bench_payload_scaling():
    header(13, "PAYLOAD SIZE vs LATENCY")
    sizes = [0, 100, 1024, 10*1024, 100*1024]
    labels = ["0B", "100B", "1KB", "10KB", "100KB"]
    N = 200
    all_results = []

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        sock = f"/tmp/clove_ps_{label}.sock"
        info(f"{label}: {N} calls per payload size...")
        with run_kernel(binary, sock) as proc:
            time.sleep(0.2)
            conn = Conn(sock)
            for _ in range(20):
                conn.call(opcode=SYS_NOOP)

            for sz, sz_label in zip(sizes, labels):
                payload = b"x" * sz
                lats = []
                for _ in range(N):
                    _, lat = conn.call(opcode=SYS_NOOP, payload=payload)
                    lats.append(lat)
                avg = mean(lats)
                all_results.append((label, sz_label, avg))
                ok(f"  {label} {sz_label:>5}: avg={avg:.4f}ms")
            conn.close()

    # Return result for 100KB (most interesting size)
    v1_100k = next(r[2] for r in all_results if r[0] == "v1" and r[1] == "100KB")
    v2_100k = next(r[2] for r in all_results if r[0] == "v2" and r[1] == "100KB")
    w, sp = compare(v1_100k, v2_100k)

    v1_0b = next(r[2] for r in all_results if r[0] == "v1" and r[1] == "0B")
    v2_0b = next(r[2] for r in all_results if r[0] == "v2" and r[1] == "0B")
    return BenchResult("Latency @100KB", round(v1_100k, 4), round(v2_100k, 4), "ms",
                       w, sp,
                       notes=f"0B: v1={v1_0b:.4f} v2={v2_0b:.4f}  100KB: v1={v1_100k:.4f} v2={v2_100k:.4f}",
                       category="payload")


def bench_shutdown():
    header(14, "SHUTDOWN TIME (SIGTERM → exit)")
    N = 10
    results = {}

    for label, binary in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        info(f"{label}: {N} iterations...")
        times = []
        for i in range(N):
            sock = f"/tmp/clove_sd_{label}_{i}.sock"
            try: os.unlink(sock)
            except: pass

            proc = subprocess.Popen(
                [str(binary), "--no-sandbox", "--socket", sock],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if proc.poll() is not None: break
                if os.path.exists(sock):
                    try:
                        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        s.settimeout(1); s.connect(sock); s.close(); break
                    except: time.sleep(0.005)
                else: time.sleep(0.005)
            time.sleep(0.1)

            t0 = time.monotonic()
            proc.terminate()
            try: proc.wait(timeout=10)
            except: proc.kill(); proc.wait(timeout=3)
            ms = (time.monotonic() - t0) * 1000
            times.append(ms)
            try: os.unlink(sock)
            except: pass

        p = percentiles(times)
        results[label] = p["p50"]
        ok(f"{label}: p50={p['p50']:.1f}ms  min={p['min']:.1f}ms  p99={p['p99']:.1f}ms")

    w, sp = compare(results["v1"], results["v2"])
    return BenchResult("Shutdown", round(results["v1"], 1), round(results["v2"], 1), "ms",
                       w, sp,
                       notes="v2 joins 16 worker threads (8 LLM + 8 async); v1 joins 8 in dtor",
                       category="shutdown")


# ════════════════════════════════════════════════════════════════
#  MAIN
# ════════════════════════════════════════════════════════════════

def main():
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    for label, path in [("v1", V1_KERNEL), ("v2", V2_KERNEL)]:
        if not path.exists():
            err(f"CLOVE {label} not found at {path}")
            sys.exit(1)

    try:
        cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                             capture_output=True, text=True).stdout.strip()
        ram_gb = int(subprocess.run(["sysctl", "-n", "hw.memsize"],
                                     capture_output=True, text=True).stdout.strip()) // (1024**3)
    except:
        cpu, ram_gb = "unknown", 0

    print(f"\n{C.B}{C.MAG}")
    print(f"  ╔══════════════════════════════════════════════════════════╗")
    print(f"  ║     CLOVE v1 vs v2 — Comprehensive Benchmark Suite     ║")
    print(f"  ║     {time.strftime('%Y-%m-%d %H:%M')}                                       ║")
    print(f"  ╚══════════════════════════════════════════════════════════╝{C.R}")
    print()
    print(f"  {C.B}System:{C.R}   {cpu}, {ram_gb}GB RAM")
    print(f"  {C.B}v1:{C.R}       {V1_KERNEL.name} ({V1_KERNEL.stat().st_size/(1024*1024):.1f} MB)")
    print(f"  {C.B}v2:{C.R}       {V2_KERNEL.name} ({V2_KERNEL.stat().st_size/(1024*1024):.1f} MB)")
    print(f"  {C.B}Protocol:{C.R}  17-byte header, magic=AGNT, shared opcodes")

    benchmarks = [
        # Startup & Resource
        bench_cold_start,
        bench_idle_memory,
        bench_memory_under_load,
        bench_binary_size,
        # Latency
        bench_noop_latency,
        bench_list_latency,
        bench_state_store_latency,
        bench_oneshot,
        # Throughput
        bench_sequential_throughput,
        bench_burst_throughput,
        bench_mixed_throughput,
        bench_multi_conn_throughput,
        # Payload
        bench_payload_scaling,
        # Shutdown
        bench_shutdown,
    ]

    results = []
    for fn in benchmarks:
        try:
            r = fn()
            if r:
                results.append(r)
        except Exception as e:
            err(f"{fn.__name__} FAILED: {e}")
            import traceback; traceback.print_exc()

    # ── Summary Table ───────────────────────────────────────────
    print(f"\n{C.B}{C.YELLOW}{'═'*76}{C.R}")
    print(f"{C.B}{C.YELLOW}  RESULTS SUMMARY{C.R}")
    print(f"{C.B}{C.YELLOW}{'═'*76}{C.R}")

    categories = ["startup", "latency", "throughput", "payload", "shutdown"]
    cat_names = {
        "startup": "STARTUP & RESOURCES",
        "latency": "IPC LATENCY",
        "throughput": "THROUGHPUT",
        "payload": "PAYLOAD SCALING",
        "shutdown": "SHUTDOWN",
    }

    v1_wins = sum(1 for r in results if r.winner == "v1")
    v2_wins = sum(1 for r in results if r.winner == "v2")

    for cat in categories:
        cat_results = [r for r in results if r.category == cat]
        if not cat_results:
            continue
        print(f"\n  {C.DIM}── {cat_names[cat]} ──{C.R}")
        for r in cat_results:
            v1_s = f"{r.v1:>12,.4f}" if isinstance(r.v1, float) and r.v1 < 100 else f"{r.v1:>12,.2f}" if isinstance(r.v1, float) else f"{r.v1:>12,}"
            v2_s = f"{r.v2:>12,.4f}" if isinstance(r.v2, float) and r.v2 < 100 else f"{r.v2:>12,.2f}" if isinstance(r.v2, float) else f"{r.v2:>12,}"

            if r.winner == "v2":
                v2_s = f"{C.GREEN}{v2_s}{C.R}"
                win_s = f"{C.GREEN}v2{C.R}"
            else:
                v1_s = f"{C.GREEN}{v1_s}{C.R}"
                win_s = f"{C.RED}v1{C.R}"

            print(f"  {r.test:<22} {v1_s}  {v2_s}  {r.unit:<7} {r.speedup:>7}  {win_s}")

    print(f"\n  {C.B}{'─'*50}{C.R}")
    print(f"  {C.GREEN}{C.B}v2 wins: {v2_wins}{C.R}   {C.RED}v1 wins: {v1_wins}{C.R}   of {len(results)} tests")

    if v2_wins > v1_wins:
        print(f"\n  {C.GREEN}{C.B}>>> v2 wins overall ({v2_wins}/{len(results)}) <<<{C.R}")
    elif v1_wins > v2_wins:
        print(f"\n  {C.RED}{C.B}>>> v1 wins overall ({v1_wins}/{len(results)}) — regressions need fixing <<<{C.R}")
    else:
        print(f"\n  {C.YELLOW}{C.B}>>> Tied <<<{C.R}")

    # Root cause analysis
    print(f"\n{C.B}{C.YELLOW}{'═'*76}{C.R}")
    print(f"{C.B}{C.YELLOW}  ROOT CAUSE ANALYSIS{C.R}")
    print(f"{C.B}{C.YELLOW}{'═'*76}{C.R}")

    regressions = [r for r in results if r.winner == "v1"]
    if regressions:
        for r in regressions:
            print(f"\n  {C.RED}{C.B}{r.test}{C.R}: v1={r.v1} vs v2={r.v2} {r.unit}")
            if r.notes:
                print(f"  {C.DIM}→ {r.notes}{C.R}")
    else:
        print(f"\n  {C.GREEN}No regressions — v2 wins across the board.{C.R}")

    print(f"\n  {C.B}Known v2 overhead (by design):{C.R}")
    print(f"  {C.DIM}• handle_message(): +chrono::now×2, +mutex, +execution_logger->record(){C.R}")
    print(f"  {C.DIM}  v1 handle_message() is: return syscall_router_->handle(msg);{C.R}")
    print(f"  {C.DIM}• Constructor: PrivacyFilter compiles 5 std::regex (SSN/email/phone/CC/IP){C.R}")
    print(f"  {C.DIM}• 16 worker threads at boot (8 LLM + 8 async) vs v1's 16 (same){C.R}")
    print(f"  {C.DIM}• Shutdown joins 16 threads explicitly vs v1's implicit dtor join{C.R}")

    print()

    # Write JSON
    ts = time.strftime("%Y%m%d_%H%M%S")
    out_path = RESULTS_DIR / f"v1_vs_v2_{ts}.json"
    json_out = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "suite": "v1-vs-v2-comprehensive",
        "system": {"os": "Darwin arm64", "cpu": cpu, "ram_gb": ram_gb},
        "binaries": {
            "v1": str(V1_KERNEL), "v2": str(V2_KERNEL),
            "v1_bytes": V1_KERNEL.stat().st_size, "v2_bytes": V2_KERNEL.stat().st_size,
        },
        "results": [asdict(r) for r in results],
        "summary": {"v1_wins": v1_wins, "v2_wins": v2_wins, "total": len(results)},
        "root_causes": {
            "memory": "PrivacyFilter 5x std::regex compile at boot (~1MB), 16 subsystems vs v1 lighter init",
            "throughput": "v2 handle_message() wraps every syscall with chrono timing + execution_logger->record() (mutex + string copies). v1 is a trivial passthrough: return syscall_router_->handle(msg)",
            "shutdown": "v2 explicitly calls llm_queue_->shutdown() + async_tasks_->shutdown() joining 16 threads. v1 only joins LLM threads in destructor"
        }
    }
    out_path.write_text(json.dumps(json_out, indent=2))
    info(f"Results saved: {out_path}")


if __name__ == "__main__":
    main()
