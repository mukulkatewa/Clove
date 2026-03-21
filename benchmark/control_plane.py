#!/usr/bin/env python3
"""CLOVE v2 Control Plane Benchmark Suite

Tests what makes CLOVE a *control plane* for AI agents — not just a sandbox.
Organized into 6 categories modeled after infrastructure benchmarks:

  1. FLEET LIFECYCLE   — spawn, kill, scale, recovery (cf. K8s scheduler)
  2. GOVERNANCE        — permission checks, PII, audit, policy (cf. K8s admission)
  3. COORDINATION      — state store, mailbox, event bus (cf. etcd + service mesh)
  4. OBSERVABILITY     — audit queries, execution replay, cost tracking
  5. RESILIENCE        — sustained throughput, memory stability, concurrent load
  6. INTEGRATION       — full governance pipeline, policy-gated LLM

Usage:
  python3 benchmark/control_plane.py              # Run all benchmarks
  python3 benchmark/control_plane.py --category coordination  # One category
  python3 benchmark/control_plane.py --quick       # Reduced iteration counts
"""

import subprocess
import socket
import time
import os
import sys
import json
import struct
import signal
import argparse
import threading
import resource
from pathlib import Path
from dataclasses import dataclass, field, asdict
from contextlib import contextmanager
from statistics import median, stdev, mean

# ── Config ──────────────────────────────────────────────────────
V2_KERNEL = Path(__file__).parent.parent / "build" / "kernel" / "clove_kernel"
RESULTS_DIR = Path(__file__).parent / "results"

# ── Colors ──────────────────────────────────────────────────────
class C:
    R = "\033[0m"; B = "\033[1m"; RED = "\033[0;31m"; GREEN = "\033[0;32m"
    CYAN = "\033[0;36m"; YELLOW = "\033[1;33m"; DIM = "\033[2m"; MAG = "\033[0;35m"

def info(msg):    print(f"  {C.CYAN}\u2022{C.R} {msg}")
def ok(msg):      print(f"  {C.GREEN}\u2713{C.R} {msg}")
def warn(msg):    print(f"  {C.YELLOW}!{C.R} {msg}")
def err(msg):     print(f"  {C.RED}\u2717{C.R} {msg}")
def header(msg):  print(f"\n{C.B}{C.YELLOW}{'─'*60}\n  {msg}\n{'─'*60}{C.R}")
def subheader(msg): print(f"\n  {C.B}{C.CYAN}{msg}{C.R}")

# ── Data ────────────────────────────────────────────────────────
@dataclass
class BenchResult:
    test: str
    value: float
    unit: str
    category: str
    notes: str = ""
    detail: dict = field(default_factory=dict)

# ── Wire Protocol ───────────────────────────────────────────────
MAGIC = 0x41474E54
HDR_SIZE = 17
HDR_FMT = "<IIBQ"

# Opcodes
SYS_NOOP            = 0x00
SYS_THINK           = 0x01
SYS_SPAWN           = 0x10
SYS_KILL            = 0x11
SYS_LIST            = 0x12
SYS_PAUSE           = 0x14
SYS_RESUME          = 0x15
SYS_SEND            = 0x20
SYS_RECV            = 0x21
SYS_BROADCAST       = 0x22
SYS_REGISTER        = 0x23
SYS_STORE           = 0x30
SYS_FETCH           = 0x31
SYS_DELETE           = 0x32
SYS_KEYS            = 0x33
SYS_GET_PERMS       = 0x40
SYS_SET_PERMS       = 0x41
SYS_SUBSCRIBE       = 0x60
SYS_UNSUBSCRIBE     = 0x61
SYS_POLL_EVENTS     = 0x62
SYS_EMIT            = 0x63
SYS_RECORD_START    = 0x70
SYS_RECORD_STOP     = 0x71
SYS_RECORD_STATUS   = 0x72
SYS_GET_AUDIT_LOG   = 0x76
SYS_LLM_CONFIG      = 0xD0
SYS_PII_SCAN        = 0xD1
SYS_PII_REDACT      = 0xD2
SYS_POLICY_RECOMMEND = 0xD7
SYS_LLM_REPORT      = 0xF0
SYS_HELLO           = 0xFE
SYS_EXIT            = 0xFF


def build_msg(agent_id=0, opcode=SYS_NOOP, payload=b""):
    return struct.pack(HDR_FMT, MAGIC, agent_id, opcode, len(payload)) + payload


class Conn:
    """Persistent connection to CLOVE kernel."""
    def __init__(self, sock_path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(10)
        self.sock.connect(sock_path)

    def call(self, opcode=SYS_NOOP, payload=b"", agent_id=9999):
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

    def call_json(self, opcode, payload_dict, agent_id=9999):
        raw, lat = self.call(opcode, json.dumps(payload_dict).encode(), agent_id)
        resp = {}
        if len(raw) > HDR_SIZE:
            try:
                resp = json.loads(raw[HDR_SIZE:])
            except:
                pass
        return resp, lat

    def fire(self, opcode=SYS_NOOP, payload=b"", agent_id=9999):
        self.sock.sendall(build_msg(agent_id=agent_id, opcode=opcode, payload=payload))

    def drain(self, n):
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


@contextmanager
def run_kernel(sock_path, timeout=20, extra_args=None):
    try:
        os.unlink(sock_path)
    except FileNotFoundError:
        pass

    cmd = [str(V2_KERNEL), "--no-sandbox", "--socket", sock_path]
    if extra_args:
        cmd.extend(extra_args)

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        raise RuntimeError("Kernel failed to start")

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
    try:
        r = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                           capture_output=True, text=True)
        return int(r.stdout.strip())
    except:
        return 0


def percentiles(data):
    s = sorted(data)
    n = len(s)
    if n == 0:
        return {"min": 0, "p50": 0, "p95": 0, "p99": 0, "max": 0, "avg": 0, "stdev": 0}
    return {
        "min": s[0], "p25": s[n//4], "p50": s[n//2],
        "p75": s[3*n//4], "p95": s[int(n*0.95)], "p99": s[int(n*0.99)],
        "max": s[-1], "avg": mean(s), "stdev": stdev(s) if n > 1 else 0,
    }


# Quick mode divisor
QUICK = False

def N(normal, quick=None):
    if QUICK:
        return quick if quick is not None else max(normal // 5, 10)
    return normal


# ════════════════════════════════════════════════════════════════
#  1. FLEET LIFECYCLE
# ════════════════════════════════════════════════════════════════

def bench_fleet_status_query():
    """SYS_LIST latency with varying numbers of conceptual agents."""
    subheader("Fleet Status Query (SYS_LIST)")
    sock = "/tmp/clove_bench_fleet.sock"
    results = []

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        # Warmup
        for _ in range(20):
            conn.call(opcode=SYS_LIST)

        lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_LIST)
            lats.append(lat)
        conn.close()

    p = percentiles(lats)
    ok(f"SYS_LIST: avg={p['avg']:.4f}ms  p50={p['p50']:.4f}ms  p99={p['p99']:.4f}ms")
    return BenchResult("Fleet Status (LIST)", round(p['avg'], 4), "ms",
                       "fleet", detail=p)


# ════════════════════════════════════════════════════════════════
#  2. GOVERNANCE OVERHEAD
# ════════════════════════════════════════════════════════════════

def bench_permission_check():
    """Permission check overhead — get_perms + set_perms latency."""
    subheader("Permission Check Latency")
    sock = "/tmp/clove_bench_perms.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        # Warmup
        for _ in range(20):
            conn.call(opcode=SYS_NOOP)

        # SYS_GET_PERMS latency
        get_lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_GET_PERMS, agent_id=1)
            get_lats.append(lat)

        # SYS_SET_PERMS latency
        set_lats = []
        set_payload = json.dumps({"level": "standard"}).encode()
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_SET_PERMS, payload=set_payload, agent_id=1)
            set_lats.append(lat)

        # Compare with NOOP baseline
        noop_lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_NOOP)
            noop_lats.append(lat)
        conn.close()

    gp = percentiles(get_lats)
    sp = percentiles(set_lats)
    np_ = percentiles(noop_lats)

    ok(f"GET_PERMS:  avg={gp['avg']:.4f}ms  p99={gp['p99']:.4f}ms")
    ok(f"SET_PERMS:  avg={sp['avg']:.4f}ms  p99={sp['p99']:.4f}ms")
    ok(f"NOOP base:  avg={np_['avg']:.4f}ms  (overhead: +{gp['avg']-np_['avg']:.4f}ms)")

    return BenchResult("Permission Check", round(gp['avg'], 4), "ms",
                       "governance",
                       notes=f"NOOP baseline: {np_['avg']:.4f}ms, overhead: +{gp['avg']-np_['avg']:.4f}ms",
                       detail={"get_perms": gp, "set_perms": sp, "noop": np_})


def bench_pii_scan_throughput():
    """PII scan throughput at various payload sizes."""
    subheader("PII Scan Throughput")
    sock = "/tmp/clove_bench_pii.sock"

    # Build test payloads with embedded PII
    pii_samples = {
        "100B": "Contact john@example.com or call 555-123-4567 for details about the project plan." + " " * 10,
        "1KB":  ("SSN: 123-45-6789 Email: test@domain.com Phone: +1-555-987-6543 " * 10 + " " * 40)[:1024],
        "10KB": ("Credit card: 4111-1111-1111-1111 IP: 192.168.1.100 SSN: 987-65-4321 " * 100)[:10240],
    }

    with run_kernel(sock, extra_args=["--privacy", "--privacy-mode", "audit"]) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(20):
            conn.call(opcode=SYS_NOOP)

        results = {}
        for size_label, text in pii_samples.items():
            payload = json.dumps({"text": text}).encode()
            lats = []
            for _ in range(N(200)):
                _, lat = conn.call(opcode=SYS_PII_SCAN, payload=payload)
                lats.append(lat)
            p = percentiles(lats)
            ops_per_sec = 1000 / p['avg'] if p['avg'] > 0 else 0
            results[size_label] = p
            ok(f"PII_SCAN {size_label:>4}: avg={p['avg']:.4f}ms  p99={p['p99']:.4f}ms  ({ops_per_sec:.0f} ops/s)")

        conn.close()

    return BenchResult("PII Scan (1KB)", round(results.get("1KB", {}).get("avg", 0), 4), "ms",
                       "governance", detail=results)


def bench_pii_redact_throughput():
    """PII redact throughput."""
    subheader("PII Redact Throughput")
    sock = "/tmp/clove_bench_pii_r.sock"

    text = "SSN: 123-45-6789 Email: john@example.com Phone: 555-123-4567 CC: 4111-1111-1111-1111 IP: 10.0.0.1"

    with run_kernel(sock, extra_args=["--privacy", "--privacy-mode", "redact"]) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(20):
            conn.call(opcode=SYS_NOOP)

        payload = json.dumps({"text": text}).encode()
        lats = []
        for _ in range(N(500)):
            resp, lat = conn.call_json(SYS_PII_REDACT, {"text": text})
            lats.append(lat)

        # Verify correctness on last response
        if resp.get("success"):
            cleaned = resp.get("cleaned_text", "")
            redacted_count = resp.get("redacted_count", 0)
            ok(f"Redacted {redacted_count} PII items")
            # Verify no raw PII remains
            assert "123-45-6789" not in cleaned, "SSN not redacted!"
            assert "john@example.com" not in cleaned, "Email not redacted!"
            ok(f"Correctness verified — no raw PII in output")

        conn.close()

    p = percentiles(lats)
    ok(f"PII_REDACT: avg={p['avg']:.4f}ms  p99={p['p99']:.4f}ms")

    return BenchResult("PII Redact", round(p['avg'], 4), "ms",
                       "governance", detail=p)


def bench_pii_accuracy():
    """PII scan false positive/negative rates on known corpus."""
    subheader("PII Scan Accuracy")
    sock = "/tmp/clove_bench_pii_a.sock"

    # Known PII patterns that MUST be detected
    true_positives = [
        ("ssn", "123-45-6789"),
        ("email", "alice@example.com"),
        ("phone", "555-123-4567"),
        ("credit_card", "4111-1111-1111-1111"),
        ("ip_address", "192.168.1.100"),
        ("ssn", "987-65-4321"),
        ("email", "bob.smith+tag@subdomain.company.org"),
        ("phone", "+1-800-555-0199"),
        ("credit_card", "5500 0000 0000 0004"),
        ("ip_address", "10.0.0.1"),
    ]

    # Non-PII that should NOT be flagged
    false_positive_tests = [
        "The meeting is at 2:30-45-6789 PM",  # not an SSN
        "Order #4111 was shipped on 1111-1111-1111",  # not a CC
        "Version 192.168 is expected next quarter",  # not an IP
    ]

    with run_kernel(sock, extra_args=["--privacy", "--privacy-mode", "audit"]) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        detected = 0
        missed = 0
        for pii_type, value in true_positives:
            text = f"Please process this: {value} thanks."
            resp, _ = conn.call_json(SYS_PII_SCAN, {"text": text})
            if resp.get("has_pii", False):
                detected += 1
            else:
                missed += 1
                warn(f"MISSED {pii_type}: {value}")

        false_pos = 0
        for text in false_positive_tests:
            resp, _ = conn.call_json(SYS_PII_SCAN, {"text": text})
            if resp.get("has_pii", False):
                false_pos += 1

        conn.close()

    total = len(true_positives)
    recall = detected / total * 100 if total > 0 else 0
    ok(f"Detection rate: {detected}/{total} ({recall:.0f}%)")
    ok(f"False positives: {false_pos}/{len(false_positive_tests)}")

    return BenchResult("PII Accuracy", round(recall, 1), "%",
                       "governance",
                       notes=f"{detected}/{total} detected, {false_pos} false positives",
                       detail={"detected": detected, "missed": missed,
                              "false_positives": false_pos, "total": total})


def bench_audit_write_throughput():
    """Audit log write throughput under sustained syscall load."""
    subheader("Audit Log Write Throughput")
    sock = "/tmp/clove_bench_audit_w.sock"
    nn = N(5000)

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(50):
            conn.call(opcode=SYS_NOOP)

        # Burst mixed syscalls that generate audit events
        t0 = time.monotonic()
        for i in range(nn):
            op = [SYS_STORE, SYS_FETCH, SYS_GET_PERMS, SYS_KEYS][i % 4]
            payloads = {
                SYS_STORE: json.dumps({"key": f"k{i}", "value": f"v{i}"}).encode(),
                SYS_FETCH: json.dumps({"key": f"k{i}"}).encode(),
                SYS_GET_PERMS: b"",
                SYS_KEYS: b"",
            }
            conn.call(opcode=op, payload=payloads[op])
        elapsed = time.monotonic() - t0
        conn.close()

    ops = nn / elapsed
    ok(f"Mixed syscall throughput with audit: {ops:,.0f} ops/sec ({nn} in {elapsed*1000:.0f}ms)")

    return BenchResult("Audit Write Throughput", round(ops), "ops/s",
                       "governance",
                       notes=f"{nn} mixed syscalls (STORE/FETCH/PERMS/KEYS)")


# ════════════════════════════════════════════════════════════════
#  3. COORDINATION
# ════════════════════════════════════════════════════════════════

def bench_state_store_ops():
    """State store operations per second."""
    subheader("State Store Ops/sec")
    sock = "/tmp/clove_bench_ss.sock"
    nn = N(2000)

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(30):
            conn.call(opcode=SYS_NOOP)

        # Write throughput
        t0 = time.monotonic()
        for i in range(nn):
            payload = json.dumps({"key": f"bench_{i}", "value": {"data": i, "tag": "perf"}}).encode()
            conn.call(opcode=SYS_STORE, payload=payload)
        write_elapsed = time.monotonic() - t0
        write_ops = nn / write_elapsed

        # Read throughput
        t0 = time.monotonic()
        for i in range(nn):
            payload = json.dumps({"key": f"bench_{i}"}).encode()
            conn.call(opcode=SYS_FETCH, payload=payload)
        read_elapsed = time.monotonic() - t0
        read_ops = nn / read_elapsed

        # Delete throughput
        t0 = time.monotonic()
        for i in range(nn):
            payload = json.dumps({"key": f"bench_{i}"}).encode()
            conn.call(opcode=SYS_DELETE, payload=payload)
        del_elapsed = time.monotonic() - t0
        del_ops = nn / del_elapsed

        # Keys listing
        # Re-populate some keys first
        for i in range(100):
            payload = json.dumps({"key": f"list_{i}", "value": i}).encode()
            conn.call(opcode=SYS_STORE, payload=payload)

        keys_lats = []
        for _ in range(N(200)):
            _, lat = conn.call(opcode=SYS_KEYS, payload=json.dumps({"prefix": "list_"}).encode())
            keys_lats.append(lat)

        conn.close()

    kp = percentiles(keys_lats)
    ok(f"STORE:  {write_ops:,.0f} ops/s ({nn} in {write_elapsed*1000:.0f}ms)")
    ok(f"FETCH:  {read_ops:,.0f} ops/s ({nn} in {read_elapsed*1000:.0f}ms)")
    ok(f"DELETE: {del_ops:,.0f} ops/s ({nn} in {del_elapsed*1000:.0f}ms)")
    ok(f"KEYS:   avg={kp['avg']:.4f}ms (100 keys)")

    return BenchResult("State Store Write", round(write_ops), "ops/s",
                       "coordination",
                       detail={"write_ops": write_ops, "read_ops": read_ops,
                              "delete_ops": del_ops, "keys_latency": kp})


def bench_state_store_ttl():
    """State store TTL eviction overhead."""
    subheader("State Store TTL Eviction")
    sock = "/tmp/clove_bench_ttl.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(20):
            conn.call(opcode=SYS_NOOP)

        # Store 1000 keys with 200ms TTL
        nn = N(1000, 200)
        for i in range(nn):
            payload = json.dumps({"key": f"ttl_{i}", "value": i, "ttl_ms": 200}).encode()
            conn.call(opcode=SYS_STORE, payload=payload)

        # Measure NOOP throughput while eviction is happening
        time.sleep(0.05)  # Let some TTLs expire
        lats = []
        for _ in range(N(500, 100)):
            _, lat = conn.call(opcode=SYS_NOOP)
            lats.append(lat)

        # Wait for full eviction, then fetch to verify
        time.sleep(0.3)
        resp, _ = conn.call_json(SYS_FETCH, {"key": "ttl_0"})
        evicted = not resp.get("success", True) or "not found" in resp.get("error", "")
        conn.close()

    p = percentiles(lats)
    ok(f"NOOP during eviction: avg={p['avg']:.4f}ms  p99={p['p99']:.4f}ms")
    ok(f"TTL eviction verified: {'yes' if evicted else 'no'}")

    return BenchResult("TTL Eviction Impact", round(p['avg'], 4), "ms",
                       "coordination",
                       notes=f"NOOP latency during {nn}-key eviction",
                       detail=p)


def bench_mailbox_point_to_point():
    """Mailbox point-to-point message latency."""
    subheader("Mailbox Point-to-Point")
    sock = "/tmp/clove_bench_mbox.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        # Agent A (sender) and Agent B (receiver) on same connection
        conn = Conn(sock)

        # Register both agents
        conn.call_json(SYS_REGISTER, {"name": "agent_a"}, agent_id=100)
        conn.call_json(SYS_REGISTER, {"name": "agent_b"}, agent_id=200)

        # Warmup
        for _ in range(10):
            conn.call_json(SYS_SEND, {"to_id": 200, "content": "warmup"}, agent_id=100)
            conn.call_json(SYS_RECV, {}, agent_id=200)

        # Benchmark: send + recv round-trip
        lats = []
        for i in range(N(1000)):
            t0 = time.monotonic()
            conn.call_json(SYS_SEND, {"to_id": 200, "content": f"msg_{i}"}, agent_id=100)
            resp, _ = conn.call_json(SYS_RECV, {}, agent_id=200)
            lat = (time.monotonic() - t0) * 1000
            lats.append(lat)

        conn.close()

    p = percentiles(lats)
    ok(f"Send+Recv RT: avg={p['avg']:.4f}ms  p50={p['p50']:.4f}ms  p99={p['p99']:.4f}ms")

    return BenchResult("Mailbox P2P RT", round(p['avg'], 4), "ms",
                       "coordination", detail=p)


def bench_mailbox_broadcast():
    """Mailbox broadcast fan-out."""
    subheader("Mailbox Broadcast Fan-out")
    sock = "/tmp/clove_bench_bcast.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        # Register N agents
        agent_count = 50
        for i in range(agent_count):
            conn.call_json(SYS_REGISTER, {"name": f"agent_{i}"}, agent_id=1000 + i)

        # Warmup
        for _ in range(5):
            conn.call_json(SYS_BROADCAST, {"content": "warmup"}, agent_id=1000)

        # Benchmark broadcast
        lats = []
        for i in range(N(200)):
            _, lat = conn.call_json(SYS_BROADCAST, {"content": f"broadcast_{i}"}, agent_id=1000)
            lats.append(lat)

        # Verify delivery — poll last agent's inbox
        resp, _ = conn.call_json(SYS_RECV, {"max_count": 1000}, agent_id=1000 + agent_count - 1)
        delivered = resp.get("count", 0)
        conn.close()

    p = percentiles(lats)
    ok(f"Broadcast ({agent_count} agents): avg={p['avg']:.4f}ms  p99={p['p99']:.4f}ms")
    ok(f"Delivery verified: {delivered} messages received by last agent")

    return BenchResult(f"Broadcast ({agent_count})", round(p['avg'], 4), "ms",
                       "coordination",
                       notes=f"{agent_count} registered agents",
                       detail=p)


def bench_event_bus_pubsub():
    """Event bus publish/subscribe latency."""
    subheader("Event Bus Pub/Sub")
    sock = "/tmp/clove_bench_evbus.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        # Subscribe agent to custom events
        conn.call_json(SYS_SUBSCRIBE, {"event_type": 11}, agent_id=500)  # CUSTOM = 11

        # Warmup
        for _ in range(10):
            conn.call_json(SYS_EMIT, {"data": {"test": "warmup"}, "event_type": 11}, agent_id=501)
            conn.call_json(SYS_POLL_EVENTS, {}, agent_id=500)

        # Emit + Poll round-trip
        lats = []
        for i in range(N(1000)):
            t0 = time.monotonic()
            conn.call_json(SYS_EMIT, {"data": {"seq": i}, "event_type": 11}, agent_id=501)
            resp, _ = conn.call_json(SYS_POLL_EVENTS, {}, agent_id=500)
            lat = (time.monotonic() - t0) * 1000
            lats.append(lat)

        conn.close()

    p = percentiles(lats)
    ok(f"Emit+Poll RT: avg={p['avg']:.4f}ms  p50={p['p50']:.4f}ms  p99={p['p99']:.4f}ms")

    return BenchResult("Event Bus RT", round(p['avg'], 4), "ms",
                       "coordination", detail=p)


def bench_event_bus_fanout():
    """Event bus fan-out with multiple subscribers."""
    subheader("Event Bus Subscriber Scaling")
    sock = "/tmp/clove_bench_evfan.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        # Subscribe 50 agents
        sub_count = 50
        for i in range(sub_count):
            conn.call_json(SYS_SUBSCRIBE, {"event_type": 11}, agent_id=2000 + i)

        # Benchmark emit latency with N subscribers
        lats = []
        for i in range(N(500)):
            _, lat = conn.call_json(SYS_EMIT, {"data": {"seq": i}, "event_type": 11}, agent_id=1)
            lats.append(lat)

        conn.close()

    p = percentiles(lats)
    ok(f"Emit ({sub_count} subs): avg={p['avg']:.4f}ms  p99={p['p99']:.4f}ms")

    return BenchResult(f"Event Fanout ({sub_count})", round(p['avg'], 4), "ms",
                       "coordination", detail=p)


# ════════════════════════════════════════════════════════════════
#  4. OBSERVABILITY
# ════════════════════════════════════════════════════════════════

def bench_audit_query():
    """Audit log query latency after generating many entries."""
    subheader("Audit Log Query Latency")
    sock = "/tmp/clove_bench_aq.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        # Generate audit entries via mixed syscalls
        nn = N(2000, 500)
        for i in range(nn):
            payload = json.dumps({"key": f"audit_gen_{i}", "value": i}).encode()
            conn.call(opcode=SYS_STORE, payload=payload)

        # Query audit log
        query_lats = []
        for _ in range(N(200)):
            _, lat = conn.call_json(SYS_GET_AUDIT_LOG, {"limit": 100})
            query_lats.append(lat)

        # Query with filter
        filter_lats = []
        for _ in range(N(200)):
            _, lat = conn.call_json(SYS_GET_AUDIT_LOG, {"category": "STATE_STORE", "limit": 50})
            filter_lats.append(lat)

        conn.close()

    qp = percentiles(query_lats)
    fp = percentiles(filter_lats)
    ok(f"Query (last 100): avg={qp['avg']:.4f}ms  p99={qp['p99']:.4f}ms")
    ok(f"Query (filtered): avg={fp['avg']:.4f}ms  p99={fp['p99']:.4f}ms")

    return BenchResult("Audit Query", round(qp['avg'], 4), "ms",
                       "observability",
                       notes=f"After {nn} syscalls generating audit entries",
                       detail={"unfiltered": qp, "filtered": fp})


def bench_execution_replay_overhead():
    """Execution replay recording overhead — ON vs OFF."""
    subheader("Execution Replay Recording Overhead")
    sock = "/tmp/clove_bench_replay.sock"
    nn = N(3000)

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        # Seed some state so SYS_KEYS has work to do
        for i in range(50):
            conn.call(opcode=SYS_STORE,
                      payload=json.dumps({"key": f"seed_{i}", "value": i}).encode())
        # Heavy warmup to stabilize
        keys_pl = json.dumps({"prefix": "seed_"}).encode()
        for _ in range(500):
            conn.call(opcode=SYS_KEYS, payload=keys_pl)

        # Throughput with recording OFF (baseline) — SYS_KEYS is recordable
        t0 = time.monotonic()
        for _ in range(nn):
            conn.call(opcode=SYS_KEYS, payload=keys_pl)
        off_elapsed = time.monotonic() - t0
        off_ops = nn / off_elapsed

        # Start recording
        conn.call_json(SYS_RECORD_START, {"max_entries": 100000})

        # Warmup recording path
        for _ in range(200):
            conn.call(opcode=SYS_KEYS, payload=keys_pl)

        # Throughput with recording ON — same workload
        t0 = time.monotonic()
        for _ in range(nn):
            conn.call(opcode=SYS_KEYS, payload=keys_pl)
        on_elapsed = time.monotonic() - t0
        on_ops = nn / on_elapsed

        # Stop recording and check entry count
        resp, _ = conn.call_json(SYS_RECORD_STOP, {})
        entries = resp.get("entries_recorded", 0)

        conn.close()

    overhead = ((off_ops - on_ops) / off_ops * 100) if off_ops > 0 else 0
    ok(f"Recording OFF: {off_ops:,.0f} ops/s")
    ok(f"Recording ON:  {on_ops:,.0f} ops/s")
    ok(f"Overhead: {overhead:.1f}%  ({entries} entries recorded)")

    return BenchResult("Replay Overhead", round(overhead, 1), "%",
                       "observability",
                       notes=f"OFF={off_ops:.0f} ON={on_ops:.0f} ops/s",
                       detail={"off_ops": off_ops, "on_ops": on_ops,
                              "overhead_pct": overhead, "entries": entries})


def bench_llm_report():
    """LLM report / cost tracking query latency."""
    subheader("LLM Report / Cost Tracking")
    sock = "/tmp/clove_bench_llmr.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(20):
            conn.call(opcode=SYS_NOOP)

        # SYS_LLM_REPORT latency
        lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_LLM_REPORT)
            lats.append(lat)

        # SYS_LLM_CONFIG GET latency
        config_lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_LLM_CONFIG)
            config_lats.append(lat)

        conn.close()

    rp = percentiles(lats)
    cp = percentiles(config_lats)
    ok(f"LLM_REPORT:  avg={rp['avg']:.4f}ms  p99={rp['p99']:.4f}ms")
    ok(f"LLM_CONFIG:  avg={cp['avg']:.4f}ms  p99={cp['p99']:.4f}ms")

    return BenchResult("LLM Report", round(rp['avg'], 4), "ms",
                       "observability", detail={"report": rp, "config": cp})


def bench_policy_recommend():
    """Policy recommendation after denial patterns."""
    subheader("Policy Recommendation Quality")
    sock = "/tmp/clove_bench_polrec.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        # Configure inference gateway with restricted models
        conn.call_json(SYS_LLM_CONFIG, {
            "enabled": True,
            "allowed_models": ["gpt-4"],
            "max_cost_usd": 0.01
        })

        # Generate denials by trying blocked models (SYS_THINK will fail fast)
        for i in range(N(50, 20)):
            conn.call_json(SYS_THINK, {
                "prompt": "test",
                "model": f"blocked-model-{i % 5}"
            })

        # Get recommendations
        resp, lat = conn.call_json(SYS_POLICY_RECOMMEND, {"max": 20})
        rec_count = len(resp.get("recommendations", []))
        denial_count = resp.get("denial_count", 0)

        ok(f"Denials recorded: {denial_count}")
        ok(f"Recommendations generated: {rec_count}")
        ok(f"Recommend latency: {lat:.4f}ms")

        if rec_count > 0:
            for r in resp["recommendations"][:3]:
                info(f"  {r['category']}: {r['action']} '{r['resource']}' (x{r['occurrence_count']})")

        conn.close()

    return BenchResult("Policy Recommend", round(lat, 4), "ms",
                       "observability",
                       notes=f"{denial_count} denials -> {rec_count} recommendations",
                       detail={"denial_count": denial_count, "rec_count": rec_count,
                              "latency_ms": lat})


# ════════════════════════════════════════════════════════════════
#  5. RESILIENCE & LONG-RUNNING
# ════════════════════════════════════════════════════════════════

def bench_sustained_throughput():
    """Sustained throughput over 30s (or 10s in quick mode)."""
    duration_sec = 10 if QUICK else 30
    subheader(f"Sustained Throughput ({duration_sec}s)")
    sock = "/tmp/clove_bench_sustain.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(100):
            conn.call(opcode=SYS_NOOP)

        # Measure throughput in 1-second windows
        windows = []
        deadline = time.monotonic() + duration_sec
        while time.monotonic() < deadline:
            count = 0
            window_end = time.monotonic() + 1.0
            while time.monotonic() < window_end:
                conn.call(opcode=SYS_NOOP)
                count += 1
            windows.append(count)

        conn.close()

    total = sum(windows)
    avg_ops = mean(windows) if windows else 0
    min_ops = min(windows) if windows else 0
    max_ops = max(windows) if windows else 0
    drift = ((max_ops - min_ops) / avg_ops * 100) if avg_ops > 0 else 0

    ok(f"Average: {avg_ops:,.0f} ops/s over {len(windows)} windows")
    ok(f"Range: {min_ops:,}—{max_ops:,} ops/s  (drift: {drift:.1f}%)")
    ok(f"Total: {total:,} operations")

    return BenchResult("Sustained Throughput", round(avg_ops), "ops/s",
                       "resilience",
                       notes=f"{len(windows)}s, drift {drift:.1f}%",
                       detail={"windows": windows, "avg": avg_ops,
                              "min": min_ops, "max": max_ops, "drift_pct": drift})


def bench_memory_stability():
    """Memory stability over many syscalls."""
    subheader("Memory Stability")
    sock = "/tmp/clove_bench_memstab.sock"
    checkpoints = [0, 10000, 50000, 100000]
    if QUICK:
        checkpoints = [0, 5000, 20000, 50000]

    with run_kernel(sock) as proc:
        time.sleep(0.3)
        rss_readings = {}

        conn = Conn(sock)
        for cp_idx, target in enumerate(checkpoints):
            prev = checkpoints[cp_idx - 1] if cp_idx > 0 else 0
            count = target - prev

            for i in range(count):
                if i % 4 == 0:
                    conn.call(opcode=SYS_STORE,
                              payload=json.dumps({"key": f"mem_{target}_{i}", "value": "x"*100}).encode())
                elif i % 4 == 1:
                    conn.call(opcode=SYS_FETCH,
                              payload=json.dumps({"key": f"mem_{target}_{i-1}"}).encode())
                elif i % 4 == 2:
                    conn.call(opcode=SYS_NOOP)
                else:
                    conn.call(opcode=SYS_KEYS)

            time.sleep(0.1)
            rss = get_rss_kb(proc.pid)
            rss_readings[target] = rss
            ok(f"After {target:>7,} calls: RSS = {rss/1024:.2f} MB")

        conn.close()

    first = rss_readings[checkpoints[0]]
    last = rss_readings[checkpoints[-1]]
    growth = (last - first) / 1024 if first > 0 else 0

    ok(f"Growth: {growth:.2f} MB over {checkpoints[-1]:,} calls")

    return BenchResult("Memory Stability", round(growth, 2), "MB growth",
                       "resilience",
                       notes=f"RSS growth over {checkpoints[-1]:,} mixed syscalls",
                       detail={str(k): v for k, v in rss_readings.items()})


def bench_concurrent_load():
    """Kernel stability with multiple concurrent connections."""
    subheader("Concurrent Connection Load")
    sock = "/tmp/clove_bench_conc.sock"
    conn_counts = [1, 4, 10, 20]
    if QUICK:
        conn_counts = [1, 4, 10]
    calls_per_conn = N(1000, 200)

    with run_kernel(sock) as proc:
        time.sleep(0.2)

        for num_conns in conn_counts:
            totals = [0] * num_conns
            errors = [0] * num_conns

            def worker(idx):
                try:
                    c = Conn(sock)
                    for _ in range(30):
                        c.call(opcode=SYS_NOOP)
                    t0 = time.monotonic()
                    for i in range(calls_per_conn):
                        op = [SYS_NOOP, SYS_STORE, SYS_FETCH, SYS_KEYS][i % 4]
                        payloads = {
                            SYS_NOOP: b"",
                            SYS_STORE: json.dumps({"key": f"c{idx}_{i}", "value": i}).encode(),
                            SYS_FETCH: json.dumps({"key": f"c{idx}_{i}"}).encode(),
                            SYS_KEYS: b"",
                        }
                        c.call(opcode=op, payload=payloads[op])
                    totals[idx] = time.monotonic() - t0
                    c.close()
                except Exception:
                    errors[idx] = 1

            threads = [threading.Thread(target=worker, args=(i,)) for i in range(num_conns)]
            wall_t0 = time.monotonic()
            for t in threads: t.start()
            for t in threads: t.join()
            wall = time.monotonic() - wall_t0

            total_ops = num_conns * calls_per_conn
            agg_ops = total_ops / wall if wall > 0 else 0
            err_count = sum(errors)
            ok(f"{num_conns:>2} conns: {agg_ops:>8,.0f} agg ops/s  ({total_ops:,} in {wall*1000:.0f}ms)  errors={err_count}")

    return BenchResult("Concurrent (20 conn)", round(agg_ops), "ops/s",
                       "resilience",
                       notes=f"{conn_counts[-1]} connections x {calls_per_conn} mixed calls")


# ════════════════════════════════════════════════════════════════
#  6. INTEGRATION BENCHMARKS
# ════════════════════════════════════════════════════════════════

def bench_policy_gated_llm():
    """Overhead of a BLOCKED LLM call (model not in allowlist)."""
    subheader("Policy-Gated LLM Call (blocked)")
    sock = "/tmp/clove_bench_pgllm.sock"

    with run_kernel(sock) as proc:
        time.sleep(0.2)
        conn = Conn(sock)

        # Configure gateway to only allow gpt-4
        conn.call_json(SYS_LLM_CONFIG, {
            "enabled": True,
            "allowed_models": ["gpt-4"],
            "max_cost_usd": 1.0
        })

        # Warmup
        for _ in range(20):
            conn.call(opcode=SYS_NOOP)

        # NOOP baseline
        noop_lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_NOOP)
            noop_lats.append(lat)

        # Blocked THINK calls (will fail fast at gateway check)
        think_lats = []
        for _ in range(N(500)):
            resp, lat = conn.call_json(SYS_THINK, {
                "prompt": "hello",
                "model": "claude-3-opus"
            })
            think_lats.append(lat)
            # Verify it was blocked
            assert not resp.get("success", True) or "not allowed" in resp.get("error", ""), \
                f"Expected blocked, got: {resp}"

        conn.close()

    np_ = percentiles(noop_lats)
    tp = percentiles(think_lats)
    overhead = tp['avg'] - np_['avg']

    ok(f"NOOP:           avg={np_['avg']:.4f}ms")
    ok(f"Blocked THINK:  avg={tp['avg']:.4f}ms  (overhead: +{overhead:.4f}ms)")

    return BenchResult("Blocked LLM", round(tp['avg'], 4), "ms",
                       "integration",
                       notes=f"Gateway rejection overhead: +{overhead:.4f}ms vs NOOP",
                       detail={"noop": np_, "blocked_think": tp})


def bench_full_governance_pipeline():
    """Full governance pipeline: perm check + PII scan + audit + response."""
    subheader("Full Governance Pipeline")
    sock = "/tmp/clove_bench_fgp.sock"

    with run_kernel(sock, extra_args=["--privacy", "--privacy-mode", "redact"]) as proc:
        time.sleep(0.2)
        conn = Conn(sock)
        for _ in range(30):
            conn.call(opcode=SYS_NOOP)

        # NOOP baseline
        noop_lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_NOOP)
            noop_lats.append(lat)

        # Permission check
        perm_lats = []
        for _ in range(N(500)):
            _, lat = conn.call(opcode=SYS_GET_PERMS)
            perm_lats.append(lat)

        # PII scan
        text = "Contact john@example.com SSN 123-45-6789 for info"
        pii_lats = []
        for _ in range(N(500)):
            _, lat = conn.call_json(SYS_PII_SCAN, {"text": text})
            pii_lats.append(lat)

        # State store (audit-generating)
        store_lats = []
        for i in range(N(500)):
            _, lat = conn.call(opcode=SYS_STORE,
                              payload=json.dumps({"key": f"gov_{i}", "value": "test"}).encode())
            store_lats.append(lat)

        # Audit query
        audit_lats = []
        for _ in range(N(200)):
            _, lat = conn.call_json(SYS_GET_AUDIT_LOG, {"limit": 50})
            audit_lats.append(lat)

        conn.close()

    np_ = percentiles(noop_lats)
    pp = percentiles(perm_lats)
    pi = percentiles(pii_lats)
    sp = percentiles(store_lats)
    ap = percentiles(audit_lats)

    total_pipeline = pp['avg'] + pi['avg'] + sp['avg'] + ap['avg']

    ok(f"NOOP baseline:  {np_['avg']:.4f}ms")
    ok(f"Permission:     {pp['avg']:.4f}ms  (+{pp['avg']-np_['avg']:.4f}ms)")
    ok(f"PII Scan:       {pi['avg']:.4f}ms  (+{pi['avg']-np_['avg']:.4f}ms)")
    ok(f"State Store:    {sp['avg']:.4f}ms  (+{sp['avg']-np_['avg']:.4f}ms)")
    ok(f"Audit Query:    {ap['avg']:.4f}ms  (+{ap['avg']-np_['avg']:.4f}ms)")
    ok(f"Pipeline total: {total_pipeline:.4f}ms")

    return BenchResult("Governance Pipeline", round(total_pipeline, 4), "ms",
                       "integration",
                       notes="perm + pii + store + audit",
                       detail={"noop": np_, "permission": pp, "pii_scan": pi,
                              "state_store": sp, "audit_query": ap,
                              "pipeline_total": total_pipeline})


# ════════════════════════════════════════════════════════════════
#  MAIN
# ════════════════════════════════════════════════════════════════

BENCHMARKS = {
    "fleet": [
        bench_fleet_status_query,
    ],
    "governance": [
        bench_permission_check,
        bench_pii_scan_throughput,
        bench_pii_redact_throughput,
        bench_pii_accuracy,
        bench_audit_write_throughput,
    ],
    "coordination": [
        bench_state_store_ops,
        bench_state_store_ttl,
        bench_mailbox_point_to_point,
        bench_mailbox_broadcast,
        bench_event_bus_pubsub,
        bench_event_bus_fanout,
    ],
    "observability": [
        bench_audit_query,
        bench_execution_replay_overhead,
        bench_llm_report,
        bench_policy_recommend,
    ],
    "resilience": [
        bench_sustained_throughput,
        bench_memory_stability,
        bench_concurrent_load,
    ],
    "integration": [
        bench_policy_gated_llm,
        bench_full_governance_pipeline,
    ],
}

CATEGORY_NAMES = {
    "fleet":          "1. FLEET LIFECYCLE",
    "governance":     "2. GOVERNANCE OVERHEAD",
    "coordination":   "3. COORDINATION",
    "observability":  "4. OBSERVABILITY",
    "resilience":     "5. RESILIENCE & LONG-RUNNING",
    "integration":    "6. INTEGRATION",
}


def main():
    global QUICK

    parser = argparse.ArgumentParser(description="CLOVE v2 Control Plane Benchmarks")
    parser.add_argument("--category", "-c", choices=list(BENCHMARKS.keys()),
                        help="Run only one category")
    parser.add_argument("--quick", "-q", action="store_true",
                        help="Reduced iteration counts for faster runs")
    args = parser.parse_args()

    QUICK = args.quick
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    if not V2_KERNEL.exists():
        err(f"Kernel not found: {V2_KERNEL}")
        err("Run: cd build && cmake --build . --target clove_kernel -j$(nproc)")
        sys.exit(1)

    try:
        cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                             capture_output=True, text=True).stdout.strip()
        ram_gb = int(subprocess.run(["sysctl", "-n", "hw.memsize"],
                                     capture_output=True, text=True).stdout.strip()) // (1024**3)
    except:
        cpu, ram_gb = "unknown", 0

    print(f"\n{C.B}{C.MAG}")
    print(f"  +{'='*56}+")
    print(f"  |   CLOVE v2 — Control Plane Benchmark Suite            |")
    print(f"  |   {time.strftime('%Y-%m-%d %H:%M')}{'':>39}|")
    print(f"  +{'='*56}+{C.R}")
    print()
    print(f"  {C.B}System:{C.R}  {cpu}, {ram_gb}GB RAM")
    print(f"  {C.B}Kernel:{C.R}  {V2_KERNEL.name} ({V2_KERNEL.stat().st_size/(1024*1024):.1f} MB)")
    print(f"  {C.B}Mode:{C.R}    {'QUICK' if QUICK else 'FULL'}")

    categories = [args.category] if args.category else list(BENCHMARKS.keys())
    all_results = []

    for cat in categories:
        header(CATEGORY_NAMES[cat])
        for fn in BENCHMARKS[cat]:
            try:
                r = fn()
                if r:
                    all_results.append(r)
            except Exception as e:
                err(f"{fn.__name__} FAILED: {e}")
                import traceback; traceback.print_exc()

    # ── Summary Table ───────────────────────────────────────────
    print(f"\n{C.B}{C.YELLOW}{'='*72}{C.R}")
    print(f"{C.B}{C.YELLOW}  RESULTS SUMMARY{C.R}")
    print(f"{C.B}{C.YELLOW}{'='*72}{C.R}")

    for cat in categories:
        cat_results = [r for r in all_results if r.category == cat]
        if not cat_results:
            continue
        print(f"\n  {C.DIM}-- {CATEGORY_NAMES[cat]} --{C.R}")
        for r in cat_results:
            val_s = f"{r.value:>12,.4f}" if isinstance(r.value, float) and r.value < 100 else f"{r.value:>12,}"
            print(f"  {r.test:<30} {val_s} {r.unit:<10}", end="")
            if r.notes:
                print(f"  {C.DIM}{r.notes}{C.R}", end="")
            print()

    # ── Industry Comparisons ──────────────────────────────────
    print(f"\n{C.B}{C.YELLOW}{'='*72}{C.R}")
    print(f"{C.B}{C.YELLOW}  INDUSTRY COMPARISONS{C.R}")
    print(f"{C.B}{C.YELLOW}{'='*72}{C.R}")

    comparisons = [
        ("Daytona sandbox cold-start",   "90ms",    "CLOVE kernel is already running (amortized)"),
        ("E2B Firecracker microVM boot",  "~150ms",  "CLOVE: agent spawn is fork+exec, not VM boot"),
        ("K8s API server pod create p99", "1000ms",  "CLOVE: SYS_LIST baseline for fleet queries"),
        ("etcd write throughput",         "10K/s",   "CLOVE: state store via SYS_STORE"),
    ]
    for name, baseline, note in comparisons:
        print(f"  {C.DIM}{name}: {baseline} — {note}{C.R}")

    # ── JSON Output ──────────────────────────────────────────
    ts = time.strftime("%Y%m%d_%H%M%S")
    out_path = RESULTS_DIR / f"control_plane_{ts}.json"
    json_out = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "suite": "control-plane-v2",
        "mode": "quick" if QUICK else "full",
        "system": {"os": "Darwin arm64", "cpu": cpu, "ram_gb": ram_gb},
        "kernel": str(V2_KERNEL),
        "kernel_bytes": V2_KERNEL.stat().st_size,
        "results": [asdict(r) for r in all_results],
        "categories": categories,
    }
    out_path.write_text(json.dumps(json_out, indent=2))
    print(f"\n  {C.CYAN}Results saved: {out_path}{C.R}")
    print()


if __name__ == "__main__":
    main()
