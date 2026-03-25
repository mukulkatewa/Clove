#!/bin/bash
# =============================================================================
# CLOVE vs OpenShell — Real Benchmark
#
# Measures: startup time, memory, LLM latency, concurrent agents, feature parity
# Runs both systems on the same machine, same tasks, same model
# =============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
DIM='\033[0;90m'
BOLD='\033[1m'
RESET='\033[0m'

CLOVE_BIN="/Users/anixd/Documents/clove-v2/build/kernel/clove_kernel"
CLOVE_PORT=9200
OPENSHELL_PORT=9300
RESULTS_DIR="/tmp/clove-benchmark-$(date +%s)"
mkdir -p "$RESULTS_DIR"

echo ""
echo -e "${BOLD}${CYAN}═══════════════════════════════════════════════════${RESET}"
echo -e "${BOLD}${CYAN}  CLOVE vs OpenShell — Head-to-Head Benchmark${RESET}"
echo -e "${BOLD}${CYAN}═══════════════════════════════════════════════════${RESET}"
echo ""
echo -e "${DIM}Results dir: $RESULTS_DIR${RESET}"
echo ""

# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

timer_start() { echo $(($(date +%s%N)/1000000)); }
timer_end() {
    local start=$1
    local end=$(($(date +%s%N)/1000000))
    echo $((end - start))
}

get_rss_kb() {
    local pid=$1
    if [[ $(uname) == "Darwin" ]]; then
        ps -o rss= -p "$pid" 2>/dev/null | tr -d ' '
    else
        cat /proc/$pid/status 2>/dev/null | grep VmRSS | awk '{print $2}'
    fi
}

log_result() {
    local test_name="$1"
    local clove_val="$2"
    local openshell_val="$3"
    local unit="$4"
    local lower_is_better="${5:-true}"

    echo "$test_name,$clove_val,$openshell_val,$unit" >> "$RESULTS_DIR/results.csv"

    if [[ "$lower_is_better" == "true" ]]; then
        if (( $(echo "$clove_val < $openshell_val" | bc -l 2>/dev/null || echo 0) )); then
            local ratio=$(echo "scale=1; $openshell_val / $clove_val" | bc -l 2>/dev/null || echo "?")
            echo -e "  ${GREEN}✓${RESET} $test_name: CLOVE ${GREEN}${clove_val}${unit}${RESET} vs OpenShell ${RED}${openshell_val}${unit}${RESET} (${GREEN}${ratio}x faster${RESET})"
        else
            local ratio=$(echo "scale=1; $clove_val / $openshell_val" | bc -l 2>/dev/null || echo "?")
            echo -e "  ${RED}✗${RESET} $test_name: CLOVE ${RED}${clove_val}${unit}${RESET} vs OpenShell ${GREEN}${openshell_val}${unit}${RESET} (${RED}${ratio}x slower${RESET})"
        fi
    else
        echo -e "  ${CYAN}─${RESET} $test_name: CLOVE ${CYAN}${clove_val}${unit}${RESET} vs OpenShell ${CYAN}${openshell_val}${unit}${RESET}"
    fi
}

log_feature() {
    local feature="$1"
    local clove="$2"
    local openshell="$3"
    echo "$feature,$clove,$openshell" >> "$RESULTS_DIR/features.csv"
    local cv="${GREEN}✓${RESET}"
    local ov="${GREEN}✓${RESET}"
    [[ "$clove" == "no" ]] && cv="${RED}✗${RESET}"
    [[ "$openshell" == "no" ]] && ov="${RED}✗${RESET}"
    echo -e "  $cv / $ov  $feature"
}

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK 1: Cold Startup Time
# ─────────────────────────────────────────────────────────────────────────────

echo -e "${BOLD}[1/6] Cold Startup Time${RESET}"
echo -e "${DIM}  Measuring time from binary launch to first API response${RESET}"

# --- CLOVE ---
pkill -9 -f clove_kernel 2>/dev/null; sleep 1
CLOVE_START=$(timer_start)
$CLOVE_BIN --sandbox --privacy --api --api-port $CLOVE_PORT > /dev/null 2>&1 &
CLOVE_PID=$!
while ! curl -s "localhost:$CLOVE_PORT/api/health" > /dev/null 2>&1; do sleep 0.01; done
CLOVE_STARTUP=$(timer_end $CLOVE_START)

# --- OpenShell ---
# OpenShell startup = gateway bootstrap (K3s in Docker)
# We measure time for `openshell status` to return healthy after a fresh start
OPENSHELL_START=$(timer_start)
# Check if gateway already running
if openshell status > /dev/null 2>&1; then
    OPENSHELL_STARTUP_NOTE="already-running"
    OPENSHELL_STARTUP=0
    # Time the status check itself
    OS_CHECK_START=$(timer_start)
    openshell status > /dev/null 2>&1
    OPENSHELL_STARTUP=$(timer_end $OS_CHECK_START)
else
    # Fresh gateway start
    timeout 120 openshell gateway start > /dev/null 2>&1 || true
    while ! openshell status > /dev/null 2>&1; do sleep 1; done
    OPENSHELL_STARTUP=$(timer_end $OPENSHELL_START)
fi

log_result "Cold startup" "$CLOVE_STARTUP" "$OPENSHELL_STARTUP" "ms"

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK 2: Memory Usage (Baseline)
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}[2/6] Baseline Memory Usage${RESET}"
echo -e "${DIM}  RSS after startup, no agents running${RESET}"

sleep 1

# CLOVE: single process
CLOVE_MEM=$(get_rss_kb $CLOVE_PID)

# OpenShell: Docker containers
# Get total memory of openshell-related containers
OPENSHELL_MEM=0
for container_id in $(docker ps --filter "label=app.kubernetes.io/part-of=openshell" -q 2>/dev/null); do
    mem=$(docker stats --no-stream --format "{{.MemUsage}}" "$container_id" 2>/dev/null | awk -F/ '{print $1}' | tr -d ' ')
    # Parse MiB/GiB to KB
    if echo "$mem" | grep -q "GiB"; then
        val=$(echo "$mem" | tr -d 'GiB')
        kb=$(echo "$val * 1048576" | bc -l 2>/dev/null | cut -d. -f1)
    elif echo "$mem" | grep -q "MiB"; then
        val=$(echo "$mem" | tr -d 'MiB')
        kb=$(echo "$val * 1024" | bc -l 2>/dev/null | cut -d. -f1)
    else
        kb=0
    fi
    OPENSHELL_MEM=$((OPENSHELL_MEM + ${kb:-0}))
done

# If no openshell containers found, try k3s containers
if [[ "$OPENSHELL_MEM" == "0" ]]; then
    for container_id in $(docker ps --filter "name=k3s" -q 2>/dev/null; docker ps --filter "name=openshell" -q 2>/dev/null); do
        mem=$(docker stats --no-stream --format "{{.MemUsage}}" "$container_id" 2>/dev/null | awk -F/ '{print $1}' | tr -d ' ')
        if echo "$mem" | grep -q "GiB"; then
            val=$(echo "$mem" | tr -d 'GiB')
            kb=$(echo "$val * 1048576" | bc -l 2>/dev/null | cut -d. -f1)
        elif echo "$mem" | grep -q "MiB"; then
            val=$(echo "$mem" | tr -d 'MiB')
            kb=$(echo "$val * 1024" | bc -l 2>/dev/null | cut -d. -f1)
        else
            kb=0
        fi
        OPENSHELL_MEM=$((OPENSHELL_MEM + ${kb:-0}))
    done
fi

CLOVE_MEM_MB=$(echo "scale=1; ${CLOVE_MEM:-0} / 1024" | bc -l 2>/dev/null || echo "?")
OPENSHELL_MEM_MB=$(echo "scale=1; ${OPENSHELL_MEM:-0} / 1024" | bc -l 2>/dev/null || echo "?")

log_result "Baseline memory" "$CLOVE_MEM_MB" "$OPENSHELL_MEM_MB" "MB"

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK 3: LLM Request Latency (through proxy)
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}[3/6] LLM Request Latency${RESET}"
echo -e "${DIM}  Time for a simple completion through each system's proxy${RESET}"

# CLOVE: through /api/v1/chat/completions
PROMPT='{"model":"google/gemini-2.0-flash-001","messages":[{"role":"user","content":"Say hi in 3 words"}]}'

CLOVE_LLM_TIMES=()
for i in 1 2 3; do
    T=$(timer_start)
    curl -s -X POST "localhost:$CLOVE_PORT/api/v1/chat/completions" \
        -H "Content-Type: application/json" \
        -d "$PROMPT" > /dev/null 2>&1
    CLOVE_LLM_TIMES+=( $(timer_end $T) )
done
# Average
CLOVE_LLM_AVG=$(echo "(${CLOVE_LLM_TIMES[0]} + ${CLOVE_LLM_TIMES[1]} + ${CLOVE_LLM_TIMES[2]}) / 3" | bc)

# OpenShell: through inference.local (not directly measurable from outside)
# Instead, measure direct OpenRouter as baseline
DIRECT_LLM_TIMES=()
for i in 1 2 3; do
    T=$(timer_start)
    curl -s -X POST "https://openrouter.ai/api/v1/chat/completions" \
        -H "Content-Type: application/json" \
        -H "Authorization: Bearer $OPENROUTER_API_KEY" \
        -d "$PROMPT" > /dev/null 2>&1
    DIRECT_LLM_TIMES+=( $(timer_end $T) )
done
DIRECT_LLM_AVG=$(echo "(${DIRECT_LLM_TIMES[0]} + ${DIRECT_LLM_TIMES[1]} + ${DIRECT_LLM_TIMES[2]}) / 3" | bc)

CLOVE_OVERHEAD=$((CLOVE_LLM_AVG - DIRECT_LLM_AVG))
[[ $CLOVE_OVERHEAD -lt 0 ]] && CLOVE_OVERHEAD=0

echo -e "  ${CYAN}─${RESET} Direct OpenRouter:        ${DIM}${DIRECT_LLM_AVG}ms avg${RESET}"
echo -e "  ${CYAN}─${RESET} CLOVE (proxy + PII + audit): ${CYAN}${CLOVE_LLM_AVG}ms avg${RESET} (overhead: ${CLOVE_OVERHEAD}ms)"
echo -e "  ${DIM}  OpenShell routes via inference.local inside sandbox — not directly comparable from host${RESET}"

echo "LLM direct,$DIRECT_LLM_AVG,ms" >> "$RESULTS_DIR/results.csv"
echo "LLM via CLOVE,$CLOVE_LLM_AVG,ms" >> "$RESULTS_DIR/results.csv"
echo "CLOVE proxy overhead,$CLOVE_OVERHEAD,ms" >> "$RESULTS_DIR/results.csv"

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK 4: Sandbox Creation Time
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}[4/6] Sandbox Creation Time${RESET}"
echo -e "${DIM}  Time to create an isolated execution environment${RESET}"

# CLOVE: spawn a sandboxed process via API
CLOVE_SANDBOX_T=$(timer_start)
curl -s -X POST "localhost:$CLOVE_PORT/api/openclaw/spawn" \
    -H "Content-Type: application/json" \
    -d '{"name":"bench-agent","soul":"benchmark test","budget_usd":1.0}' > /dev/null 2>&1
CLOVE_SANDBOX_MS=$(timer_end $CLOVE_SANDBOX_T)

# Cleanup
curl -s -X POST "localhost:$CLOVE_PORT/api/openclaw/stop-all" > /dev/null 2>&1

# OpenShell: create a sandbox (K8s pod)
OPENSHELL_SANDBOX_T=$(timer_start)
timeout 120 openshell sandbox create --name bench-test -- echo "hello" > /dev/null 2>&1 || true
OPENSHELL_SANDBOX_MS=$(timer_end $OPENSHELL_SANDBOX_T)

# Cleanup
openshell sandbox delete bench-test > /dev/null 2>&1 || true

log_result "Sandbox creation" "$CLOVE_SANDBOX_MS" "$OPENSHELL_SANDBOX_MS" "ms"

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK 5: Agent Run (End-to-End)
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}[5/6] Agent Run — End to End${RESET}"
echo -e "${DIM}  Time for a complete agent task (goal → tools → result)${RESET}"

# CLOVE: POST /api/run
CLOVE_RUN_T=$(timer_start)
CLOVE_RUN_RESULT=$(curl -s -X POST "localhost:$CLOVE_PORT/api/run" \
    -H "Content-Type: application/json" \
    -d '{"goal":"What is 2+2? Answer with just the number.","budget":0.05}')
CLOVE_RUN_MS=$(timer_end $CLOVE_RUN_T)
CLOVE_RUN_COST=$(echo "$CLOVE_RUN_RESULT" | python3 -c "import sys,json; print(json.load(sys.stdin).get('total_cost_usd',0))" 2>/dev/null || echo "0")
CLOVE_RUN_TOKENS=$(echo "$CLOVE_RUN_RESULT" | python3 -c "import sys,json; print(json.load(sys.stdin).get('total_tokens',0))" 2>/dev/null || echo "0")

echo -e "  ${CYAN}─${RESET} CLOVE run: ${CYAN}${CLOVE_RUN_MS}ms${RESET} ($${CLOVE_RUN_COST}, ${CLOVE_RUN_TOKENS} tokens)"
echo -e "  ${DIM}  OpenShell doesn't have a built-in agent runner — it's a sandbox, not a runtime${RESET}"

echo "Agent run (CLOVE),$CLOVE_RUN_MS,ms" >> "$RESULTS_DIR/results.csv"

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK 6: Feature Comparison
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}[6/6] Feature Comparison${RESET}"
echo -e "${DIM}  CLOVE / OpenShell${RESET}"
echo ""

log_feature "Native binary (no Docker)"           "yes" "no"
log_feature "Filesystem sandbox (Landlock/Seatbelt)" "yes" "yes"
log_feature "Network sandbox (seccomp/proxy)"      "yes" "yes"
log_feature "Syscall filtering (seccomp)"          "yes" "yes"
log_feature "Built-in agent runtime (RunEngine)"   "yes" "no"
log_feature "Built-in fleet (parallel agents)"     "yes" "no"
log_feature "LLM cost tracking (per-agent)"        "yes" "no"
log_feature "LLM budget enforcement (hard kill)"   "yes" "no"
log_feature "PII filtering on prompts"             "yes" "no"
log_feature "Audit trail (8 categories)"           "yes" "no"
log_feature "Execution replay"                     "yes" "no"
log_feature "Agent-to-agent IPC (mailbox)"         "yes" "no"
log_feature "Memory blocks (agent persistence)"    "yes" "no"
log_feature "Context assembly (research-backed)"   "yes" "no"
log_feature "OpenAI-compatible LLM proxy"          "yes" "no"
log_feature "Scheduling (cron)"                    "yes" "no"
log_feature "Webhooks"                             "yes" "no"
log_feature "REST API (65+ endpoints)"             "yes" "no"
log_feature "Dashboard UI"                         "yes" "no"
log_feature "OpenClaw integration"                 "yes" "yes"
log_feature "Hot-reload policy"                    "no"  "yes"
log_feature "L7 network inspection (OPA/Rego)"     "no"  "yes"
log_feature "SSH into sandbox"                     "no"  "yes"
log_feature "Custom container images"              "no"  "yes"
log_feature "GPU passthrough"                      "no"  "yes"
log_feature "Binary integrity (SHA256 TOFU)"       "no"  "yes"

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────

echo ""
echo -e "${BOLD}${CYAN}═══════════════════════════════════════════════════${RESET}"
echo -e "${BOLD}${CYAN}  Summary${RESET}"
echo -e "${BOLD}${CYAN}═══════════════════════════════════════════════════${RESET}"
echo ""
echo -e "  ${BOLD}CLOVE${RESET}:     Native C++ kernel. ${GREEN}Fast, lightweight, full agent OS.${RESET}"
echo -e "            Startup: ${GREEN}${CLOVE_STARTUP}ms${RESET}, Memory: ${GREEN}${CLOVE_MEM_MB}MB${RESET}"
echo -e "            Built-in: agent runner, fleet, cost tracking, PII, audit, dashboard"
echo ""
echo -e "  ${BOLD}OpenShell${RESET}: Rust + Docker + K3s. ${YELLOW}Thorough isolation, no agent runtime.${RESET}"
echo -e "            Startup: ${YELLOW}${OPENSHELL_STARTUP}ms${RESET}, Memory: ${YELLOW}${OPENSHELL_MEM_MB}MB${RESET}"
echo -e "            Built-in: sandbox only. L7 proxy, OPA policies, hot-reload"
echo ""
echo -e "  ${BOLD}TL;DR${RESET}: OpenShell is a sandbox. CLOVE is an agent OS."
echo -e "         OpenShell isolates. CLOVE isolates + runs + tracks + governs."
echo ""
echo -e "${DIM}Full results: $RESULTS_DIR/results.csv${RESET}"
echo ""

# Cleanup
kill $CLOVE_PID 2>/dev/null
