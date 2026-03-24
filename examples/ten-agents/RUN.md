# How to Run the 10-Agent Experiment

## Prerequisites

1. CLOVE v2 built (from repo root):
```bash
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j$(sysctl -n hw.ncpu 2>/dev/null || nproc) && cd ..
```

2. OpenRouter API key in `examples/.env`:
```
OPENROUTER_API_KEY=sk-or-v1-your-key-here
```

---

## Option A: Quick Test (2 minutes)

Runs 10 agents in parallel, one cycle, produces one report.

```bash
cd /Users/anixd/Documents/clove-v2
python3 examples/ten-agents/run.py "AI agent security in 2026"
```

Output: results in terminal + `examples/ten-agents/results.json`

---

## Option B: Research Station (30 minutes)

Runs continuously, producing a new research report every 3 minutes.

```bash
cd /Users/anixd/Documents/clove-v2
python3 examples/ten-agents/research_station.py
```

To run for a different duration (e.g., 10 minutes):
```bash
python3 examples/ten-agents/research_station.py 10
```

Output:
- `examples/ten-agents/reports/` — markdown reports (one per cycle)
- `examples/ten-agents/station_metrics.json` — final metrics
- Terminal shows live progress

---

## Option C: Full Stack (kernel + agents + dashboard)

The real demo. Three terminals.

**Terminal 1 — Start CLOVE kernel:**
```bash
cd /Users/anixd/Documents/clove-v2

export OPENROUTER_API_KEY=$(grep OPENROUTER_API_KEY examples/.env | cut -d= -f2)

./build/kernel/clove_kernel \
    --api --api-port 8080 \
    --openrouter --openrouter-key $OPENROUTER_API_KEY \
    --privacy \
    --db /tmp/clove-experiment.db
```

**Terminal 2 — Run research station:**
```bash
cd /Users/anixd/Documents/clove-v2
python3 examples/ten-agents/research_station.py 30
```

**Terminal 3 — Monitor:**
```bash
cd /Users/anixd/Documents/clove-v2

# Check kernel health
./build/cli/clove_cli status

# Watch agents
./build/cli/clove_cli ps

# View audit trail
./build/cli/clove_cli audit --limit 20

# Check metrics
./build/cli/clove_cli metrics

# Open dashboard
open http://localhost:8080/dashboard
```

---

## What You'll See

After 30 minutes:
- 10 research cycles completed
- 10 markdown reports in `examples/ten-agents/reports/`
- 30+ LLM calls made through OpenRouter
- Full metrics in `station_metrics.json`
- Dashboard showing live data (if kernel running)
- Audit trail of every action

## Expected Costs

Using `google/gemini-2.0-flash-001`:
- ~$0.00 per cycle (Gemini Flash is nearly free)
- 30-minute run total: < $0.01

Using `openai/gpt-4o-mini`:
- ~$0.001 per cycle
- 30-minute run total: ~$0.01

Using `anthropic/claude-3.5-sonnet`:
- ~$0.01-0.02 per cycle
- 30-minute run total: ~$0.10-0.20
