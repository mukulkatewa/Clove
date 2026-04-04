#!/usr/bin/env node
/**
 * benchmark_memory.mjs — CLOVE memory system benchmarks
 *
 * Measures two marketing metrics:
 *   CPR  (Cost Per Run)           — tokens_consumed / jobs_completed
 *   LTCR (Long-Run Task Completion Rate) — jobs_completed / jobs_started
 *
 * Usage:
 *   source .env.kernel
 *   node tools/benchmark_memory.mjs [--steps 20|50|100|200] [--runs 10] [--workspace <id>]
 *
 * Requires kernel running on localhost:8080.
 */

const BASE = process.env.KERNEL_URL || "http://localhost:8080";

// ── CLI args ────────────────────────────────────────────────────────────────
const args = process.argv.slice(2);
const getArg = (flag, def) => {
  const i = args.indexOf(flag);
  return i >= 0 ? args[i + 1] : def;
};
const MAX_STEPS = parseInt(getArg("--steps", "20"), 10);
const NUM_RUNS  = parseInt(getArg("--runs",  "10"),  10);
const WORKSPACE = getArg("--workspace", "bench-ws");

// ── Benchmark tasks — varied complexity ──────────────────────────────────────
const TASKS = [
  "List all files in the current directory and count them.",
  "Remember the fact: the sky is blue. Then recall: what color is the sky?",
  "Search for 'CLOVE kernel architecture' and summarize the top result.",
  "Write a file called /tmp/bench_test.txt with content 'hello world', then read it back.",
  "What is 2 + 2? Use the exec tool to run: echo $((2 + 2))",
];

// ── Helpers ──────────────────────────────────────────────────────────────────
async function submitJob(goal, workspaceId) {
  const res = await fetch(`${BASE}/api/jobs`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      goal,
      agent_name: "bench-agent",
      workspace_id: workspaceId,
      model: "openai/gpt-4o-mini",
      budget_usd: 0.10,
      max_steps: MAX_STEPS,
      allowed_tools: ["read_file", "write_file", "exec", "search", "remember", "recall"],
    }),
  });
  if (!res.ok) throw new Error(`submit failed: ${res.status} ${await res.text()}`);
  const body = await res.json();
  return body.id || body.job_id;
}

async function pollJob(jobId, timeoutMs = 120_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    await new Promise(r => setTimeout(r, 2000));
    const res = await fetch(`${BASE}/api/jobs/${jobId}`);
    if (!res.ok) continue;
    const job = await res.json();
    if (job.status === "completed" || job.status === "failed" || job.status === "cancelled") {
      return job;
    }
  }
  return null; // timed out
}

async function ensureWorkspace(name) {
  // Try to find existing world/workspace
  const list = await fetch(`${BASE}/api/worlds`);
  if (list.ok) {
    const ws = await list.json();
    const arr = Array.isArray(ws) ? ws : (ws.worlds || ws.workspaces || []);
    const found = arr.find(w => w.name === name || w.id === name);
    if (found) return found.id;
  }
  // No workspace needed — jobs run without one
  return "";
}

// ── Run benchmark ─────────────────────────────────────────────────────────────
async function runBenchmark() {
  console.log(`\n═══ CLOVE Memory Benchmark ═══`);
  console.log(`  max_steps=${MAX_STEPS}  runs=${NUM_RUNS}  workspace=${WORKSPACE}`);
  console.log(`  kernel=${BASE}\n`);

  const workspaceId = await ensureWorkspace(WORKSPACE);

  let jobsStarted = 0;
  let jobsCompleted = 0;
  let totalTokens = 0;
  let totalCostUsd = 0;
  const results = [];

  for (let i = 0; i < NUM_RUNS; i++) {
    const task = TASKS[i % TASKS.length];
    console.log(`[${i + 1}/${NUM_RUNS}] Submitting: "${task.slice(0, 60)}…"`);

    let jobId;
    try {
      jobId = await submitJob(task, workspaceId);
      jobsStarted++;
    } catch (e) {
      console.error(`  ✗ Submit failed: ${e.message}`);
      continue;
    }

    const job = await pollJob(jobId);
    if (!job) {
      console.warn(`  ⏱ Timed out: job ${jobId}`);
      continue;
    }

    const tokens = job.tokens || 0;
    const cost   = job.cost_usd || 0;
    const steps  = job.steps_done || 0;
    const ok     = job.status === "completed";

    if (ok) jobsCompleted++;
    totalTokens   += tokens;
    totalCostUsd  += cost;

    results.push({ jobId, status: job.status, tokens, cost, steps, task: task.slice(0, 50) });

    const icon = ok ? "✓" : "✗";
    console.log(`  ${icon} status=${job.status}  steps=${steps}  tokens=${tokens}  cost=$${cost.toFixed(4)}`);
  }

  // ── Metrics ────────────────────────────────────────────────────────────────
  const CPR  = jobsCompleted > 0 ? (totalTokens / jobsCompleted).toFixed(0) : "N/A";
  const LTCR = jobsStarted   > 0 ? ((jobsCompleted / jobsStarted) * 100).toFixed(1) : "N/A";

  console.log(`\n── Results ──────────────────────────────────`);
  console.log(`  Jobs started:    ${jobsStarted}`);
  console.log(`  Jobs completed:  ${jobsCompleted}`);
  console.log(`  Total tokens:    ${totalTokens}`);
  console.log(`  Total cost:      $${totalCostUsd.toFixed(4)}`);
  console.log(`\n  📊 CPR  (tokens / completed run): ${CPR} tokens/run`);
  console.log(`  📊 LTCR (completion rate @${MAX_STEPS} steps): ${LTCR}%`);
  console.log(`─────────────────────────────────────────────\n`);

  // ── JSON summary ───────────────────────────────────────────────────────────
  const summary = {
    timestamp: new Date().toISOString(),
    config: { max_steps: MAX_STEPS, num_runs: NUM_RUNS, workspace: WORKSPACE },
    metrics: {
      CPR:  CPR === "N/A" ? null : parseInt(CPR),
      LTCR: LTCR === "N/A" ? null : parseFloat(LTCR),
    },
    totals: { jobs_started: jobsStarted, jobs_completed: jobsCompleted, tokens: totalTokens, cost_usd: totalCostUsd },
    runs: results,
  };
  const outFile = `bench_results_${MAX_STEPS}steps_${Date.now()}.json`;
  try {
    const fs = await import("fs");
    fs.writeFileSync(outFile, JSON.stringify(summary, null, 2));
    console.log(`  Results written to: ${outFile}`);
  } catch {}

  return summary;
}

runBenchmark().catch(e => { console.error(e); process.exit(1); });
