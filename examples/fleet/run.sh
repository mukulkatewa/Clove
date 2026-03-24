#!/usr/bin/env bash
set -euo pipefail

# ─── CLOVE 10-Agent Fleet ───
# 10 real processes, all connected to the kernel via Unix sockets,
# coordinating through IPC and events. Not threads. Not functions. Processes.
#
# Usage:
#   ./build/kernel/clove_kernel &    # start kernel first
#   bash examples/fleet/run.sh       # launch 10 agents
#   bash examples/fleet/run.sh 5     # run for 5 minutes (default: 30)

DURATION_MIN=${1:-30}
AGENT_DIR="$(cd "$(dirname "$0")/agents" && pwd)"
PIDS=()

cleanup() {
    echo ""
    echo "  Shutting down fleet..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null
    echo "  All agents stopped."

    # Print final kernel metrics
    echo ""
    echo "  ═══ Final Kernel State ═══"
    curl -s localhost:8080/api/health 2>/dev/null | python3 -m json.tool 2>/dev/null || true
    curl -s localhost:8080/api/metrics 2>/dev/null | python3 -m json.tool 2>/dev/null || true
    echo ""
    echo "  Reports: examples/fleet/reports/"
    ls examples/fleet/reports/*.md 2>/dev/null | wc -l | xargs -I{} echo "  {} reports generated"
}

trap cleanup EXIT INT TERM

echo "═══════════════════════════════════════════════════════════════"
echo "  CLOVE 10-Agent Fleet"
echo "  10 real processes · kernel IPC · event coordination"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Check kernel
if ! curl -s localhost:8080/api/health > /dev/null 2>&1; then
    echo "  ERROR: Kernel not running."
    echo "  Start it:  ./build/kernel/clove_kernel &"
    exit 1
fi

echo "  Kernel: $(curl -s localhost:8080/api/health | python3 -c 'import sys,json; d=json.load(sys.stdin); print(f"v{d[\"version\"]} | {d[\"syscall_count\"]} syscalls | uptime {d[\"uptime_s\"]}s")')"
echo "  Duration: ${DURATION_MIN} minutes"
echo "  Dashboard: http://localhost:8080/dashboard"
echo ""

# Start execution recording
python3 -c "
import sys; sys.path.insert(0, 'sdk/python')
from clove_sdk.client import CloveClient
c = CloveClient(); c.connect(); c.record_start(); c.disconnect()
print('  Execution recording: ON')
"

echo ""
echo "  Launching 10 agents..."
echo ""

# Launch all 10 agent processes
cd "$AGENT_DIR"

python3 coordinator.py &
PIDS+=($!)
echo "  [PID $!] coordinator"

for i in 1 2 3; do
    python3 researcher.py "$i" &
    PIDS+=($!)
    echo "  [PID $!] researcher-$i"
done

for i in 1 2 3; do
    python3 writer.py "$i" &
    PIDS+=($!)
    echo "  [PID $!] writer-$i"
done

for i in 1 2; do
    python3 reviewer.py "$i" &
    PIDS+=($!)
    echo "  [PID $!] reviewer-$i"
done

python3 monitor.py &
PIDS+=($!)
echo "  [PID $!] monitor"

echo ""
echo "  All 10 agents launched. Running for ${DURATION_MIN} minutes..."
echo "  Ctrl+C to stop early."
echo ""

# Wait for duration
sleep $((DURATION_MIN * 60)) &
SLEEP_PID=$!
wait $SLEEP_PID 2>/dev/null || true
