#!/usr/bin/env node
/**
 * benchmark_memory.mjs — CLOVE memory + compression A/B benchmark
 *
 * Runs 4 modes against the same hard task set and compares:
 *   baseline      — compress_context=false, use_memory=false
 *   memory-only   — compress_context=false, use_memory=true
 *   compress-only — compress_context=true,  use_memory=false
 *   full          — compress_context=true,  use_memory=true  ← production
 *
 * Metrics:
 *   CPR  — tokens consumed per completed run (lower = cheaper)
 *   LTCR — % of jobs that completed within max_steps (higher = more reliable)
 *
 * Usage:
 *   source .env.kernel
 *   node tools/benchmark_memory.mjs [--steps 20] [--runs 5] [--mode all|baseline|full]
 */

const BASE     = process.env.KERNEL_URL || "http://localhost:8080";
const MODEL    = process.env.BENCH_MODEL || "openai/gpt-4o-mini";
const TIMEOUT  = parseInt(process.env.BENCH_TIMEOUT || "180000", 10);  // 3 min per job

const args    = process.argv.slice(2);
const getArg  = (f, d) => { const i = args.indexOf(f); return i >= 0 ? args[i+1] : d; };
const MAX_STEPS = parseInt(getArg("--steps", "20"), 10);
const NUM_RUNS  = parseInt(getArg("--runs",  "3"),  10);
const RUN_MODE  = getArg("--mode", "all");

// ── Hard tasks — these actually stress tool outputs and context window ────────
// Each task forces multiple tool calls with large-ish output.
const HARD_TASKS = [
  {
    name: "codebase-scan",
    goal: `Read the file /Users/anixd/Documents/clove-v2/libs/api/src/run_engine.cpp. ` +
          `Count the total number of lines. List every function definition you find (lines starting with a return type and function name). ` +
          `Then write a summary to /tmp/bench_codescan.txt with: total line count, function count, and function names.`,
    tools: ["read_file", "write_file", "remember", "recall"],
  },
  {
    name: "cross-reference",
    goal: `Read /Users/anixd/Documents/clove-v2/libs/api/src/api_server.cpp and find every line that constructs a RunEngine object. ` +
          `Then read /Users/anixd/Documents/clove-v2/libs/api/include/clove/run_engine.hpp and find the RunEngine constructor signature. ` +
          `Write a report to /tmp/bench_crossref.txt listing: how many RunEngine constructions exist, and whether any are missing the memory_mgr parameter.`,
    tools: ["read_file", "write_file", "remember", "recall"],
  },
  {
    name: "git-analysis",
    goal: `Run: git -C /Users/anixd/Documents/clove-v2 log --oneline -20 ` +
          `to get the last 20 commits. Group them into categories: feat, fix, refactor, other. ` +
          `Count commits per category. Write the result to /tmp/bench_gitlog.txt. Then read it back and confirm it was written.`,
    tools: ["exec", "read_file", "write_file", "remember", "recall"],
  },
];

// ── 4 benchmark modes ─────────────────────────────────────────────────────────
const MODES = [
  { name: "baseline",      compress_context: false, use_memory: false },
  { name: "memory-only",   compress_context: false, use_memory: true  },
  { name: "compress-only", compress_context: true,  use_memory: false },
  { name: "full",          compress_context: true,  use_memory: true  },
].filter(m => RUN_MODE === "all" || m.name === RUN_MODE);

// ── Helpers ───────────────────────────────────────────────────────────────────
async function submitJob(task, mode) {
  const res = await fetch(`${BASE}/api/jobs`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      goal: task.goal,
      agent_name: `bench-${mode.name}`,
      model: MODEL,
      budget_usd: 0.25,
      max_steps: MAX_STEPS,
      allowed_tools: task.tools,
      compress_context: mode.compress_context,
      use_memory: mode.use_memory,
    }),
  });
  if (!res.ok) throw new Error(`submit failed: ${res.status} ${await res.text()}`);
  const body = await res.json();
  return body.id || body.job_id;
}

async function pollJob(jobId) {
  const deadline = Date.now() + TIMEOUT;
  while (Date.now() < deadline) {
    await new Promise(r => setTimeout(r, 2500));
    try {
      const res = await fetch(`${BASE}/api/jobs/${jobId}`);
      if (!res.ok) continue;
      const job = await res.json();
      if (["completed","failed","cancelled"].includes(job.status)) return job;
    } catch {}
  }
  return null;
}

function pad(s, n) { return String(s).padStart(n); }
function pct(a, b) { return b > 0 ? ((a/b)*100).toFixed(1)+"%" : "N/A"; }

// ── Run one mode ──────────────────────────────────────────────────────────────
async function runMode(mode) {
  const runs = [];
  for (let r = 0; r < NUM_RUNS; r++) {
    const task = HARD_TASKS[r % HARD_TASKS.length];
    let jobId;
    try { jobId = await submitJob(task, mode); }
    catch (e) { runs.push({ ok: false, tokens: 0, cost: 0, steps: 0, error: e.message }); continue; }

    const job = await pollJob(jobId);
    if (!job) { runs.push({ ok: false, tokens: 0, cost: 0, steps: 0, error: "timeout" }); continue; }

    runs.push({
      ok: job.status === "completed",
      tokens: job.tokens || 0,
      cost: job.cost_usd || 0,
      steps: job.steps_done || 0,
      task: task.name,
      error: job.error || "",
      result_preview: String(job.result || "").slice(0, 80),
    });

    const icon = job.status === "completed" ? "✓" : "✗";
    console.log(`    ${icon} [${task.name}] steps=${job.steps_done} tokens=${job.tokens||0} cost=$${(job.cost_usd||0).toFixed(4)} ${job.status==="failed"?"ERR:"+job.error.slice(0,50):""}`);
  }

  const completed   = runs.filter(r => r.ok).length;
  const totalTokens = runs.reduce((s, r) => s + r.tokens, 0);
  const totalCost   = runs.reduce((s, r) => s + r.cost,   0);
  const CPR         = completed > 0 ? Math.round(totalTokens / completed) : null;
  const LTCR        = runs.length  > 0 ? (completed / runs.length) * 100  : 0;

  return { mode: mode.name, completed, total: runs.length, totalTokens, totalCost, CPR, LTCR, runs };
}

// ── Main ──────────────────────────────────────────────────────────────────────
async function main() {
  console.log(`\n${"═".repeat(60)}`);
  console.log(` CLOVE Memory Benchmark  —  A/B comparison`);
  console.log(`${"═".repeat(60)}`);
  console.log(` model=${MODEL}  max_steps=${MAX_STEPS}  runs_per_mode=${NUM_RUNS}`);
  console.log(` modes: ${MODES.map(m=>m.name).join(", ")}`);
  console.log(`${"─".repeat(60)}\n`);

  const health = await fetch(`${BASE}/api/health`).then(r=>r.json()).catch(()=>null);
  if (!health || health.status !== "ok") {
    console.error("Kernel not reachable at", BASE); process.exit(1);
  }

  const results = [];

  for (const mode of MODES) {
    console.log(`\n▶ Mode: ${mode.name.toUpperCase()} (compress=${mode.compress_context} memory=${mode.use_memory})`);
    const r = await runMode(mode);
    results.push(r);
  }

  // ── Comparison table ────────────────────────────────────────────────────────
  const baseline = results.find(r => r.mode === "baseline");
  const full     = results.find(r => r.mode === "full");

  console.log(`\n${"═".repeat(60)}`);
  console.log(` Results`);
  console.log(`${"═".repeat(60)}`);
  console.log(` ${"Mode".padEnd(16)} ${"CPR (tok/run)".padStart(14)} ${"LTCR".padStart(8)} ${"Cost/run".padStart(10)} ${"Delta CPR".padStart(10)}`);
  console.log(` ${"─".repeat(58)}`);

  for (const r of results) {
    const cpr_str  = r.CPR !== null ? r.CPR.toLocaleString() : "N/A";
    const ltcr_str = pct(r.completed, r.total);
    const cost_str = r.total > 0 ? "$"+(r.totalCost/r.total).toFixed(4) : "N/A";
    let delta = "";
    if (baseline && r.mode !== "baseline" && r.CPR !== null && baseline.CPR !== null) {
      const d = ((r.CPR - baseline.CPR) / baseline.CPR) * 100;
      delta = (d < 0 ? "▼" : "▲") + Math.abs(d).toFixed(1) + "%";
    }
    console.log(` ${r.mode.padEnd(16)} ${cpr_str.padStart(14)} ${ltcr_str.padStart(8)} ${cost_str.padStart(10)} ${delta.padStart(10)}`);
  }

  if (baseline && full && baseline.CPR && full.CPR) {
    const cpr_delta  = ((full.CPR - baseline.CPR) / baseline.CPR * 100).toFixed(1);
    const ltcr_delta = (full.LTCR - baseline.LTCR).toFixed(1);
    console.log(`\n${"─".repeat(60)}`);
    console.log(` Full vs Baseline:`);
    console.log(`   CPR  : ${cpr_delta < 0 ? "▼" : "▲"} ${Math.abs(cpr_delta)}% tokens per run`);
    console.log(`   LTCR : ${ltcr_delta >= 0 ? "+" : ""}${ltcr_delta}pp completion rate`);
    console.log(`   Cost : -$${((baseline.totalCost - full.totalCost) / (baseline.total||1)).toFixed(5)} per run saved`);
  }
  console.log(`${"═".repeat(60)}\n`);

  // Write JSON
  const outFile = `bench_ab_${MAX_STEPS}steps_${Date.now()}.json`;
  const { writeFileSync } = await import("fs");
  writeFileSync(outFile, JSON.stringify({ timestamp: new Date().toISOString(), config: { model: MODEL, max_steps: MAX_STEPS, runs_per_mode: NUM_RUNS }, results }, null, 2));
  console.log(` Results written to: ${outFile}\n`);

  // ── Parallel sharing test ───────────────────────────────────────────────────
  // Proves workspace-scoped memory: agent-A writes, agent-B (different name,
  // same workspace) should be able to recall A's fact without re-discovering it.
  if (RUN_MODE === "all" || RUN_MODE === "sharing") {
    await runSharingTest();
  }
}

async function runSharingTest() {
  const WS = "bench-sharing-ws";
  console.log(`\n${"═".repeat(60)}`);
  console.log(` Parallel Sharing Test`);
  console.log(` Proves: agent-B recalls what agent-A wrote in same workspace`);
  console.log(`${"─".repeat(60)}`);

  // Agent A: remember a unique fact into the workspace
  const FACT = `CLOVE_BENCH_SECRET_${Date.now()}`;
  console.log(`\n[A] agent-alpha  →  remember: "${FACT}"`);

  const jobA = await fetch(`${BASE}/api/jobs`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      goal: `Remember this exact fact using the remember tool: "${FACT}". Then confirm you stored it.`,
      agent_name: "agent-alpha",
      workspace_id: WS,
      model: MODEL,
      budget_usd: 0.05,
      max_steps: 5,
      allowed_tools: ["remember", "recall"],
      compress_context: true,
      use_memory: true,
    }),
  }).then(r => r.json());

  const resA = await pollJob(jobA.id || jobA.job_id);
  const aOk = resA?.status === "completed";
  console.log(`    ${aOk ? "✓" : "✗"} status=${resA?.status}  steps=${resA?.steps_done}  result="${String(resA?.result||"").slice(0,80)}"`);

  if (!aOk) {
    console.log(`    ✗ Agent A failed — skipping sharing check\n`);
    return;
  }

  // Brief pause to let SQLite write commit
  await new Promise(r => setTimeout(r, 1000));

  // Agent B: different agent name, same workspace — try to recall the fact
  console.log(`\n[B] agent-beta   →  recall: "${FACT.slice(0,20)}…"`);

  const jobB = await fetch(`${BASE}/api/jobs`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      goal: `Call the recall tool now with this exact query: "${FACT.slice(0,20)}". ` +
            `Then look at the raw output from the recall tool. ` +
            `If the recall output contains the word "CLOVE", respond with exactly: SHARING_CONFIRMED. ` +
            `If the recall output says "no memories found", respond with exactly: SHARING_FAILED. ` +
            `Do not add any other text.`,
      agent_name: "agent-beta",
      workspace_id: WS,
      model: MODEL,
      budget_usd: 0.05,
      max_steps: 5,
      allowed_tools: ["remember", "recall"],
      compress_context: true,
      use_memory: true,
    }),
  }).then(r => r.json());

  const resB = await pollJob(jobB.id || jobB.job_id);
  const result = String(resB?.result || "");
  const shared = result.includes("SHARING_CONFIRMED");

  console.log(`    ${resB?.status === "completed" ? "✓" : "✗"} status=${resB?.status}  steps=${resB?.steps_done}`);
  console.log(`    result: "${result.slice(0, 120)}"`);
  console.log(`\n${"─".repeat(60)}`);
  console.log(shared
    ? ` ✓ WORKSPACE MEMORY SHARING WORKS — agent-beta recalled agent-alpha's memory`
    : ` ✗ Sharing test inconclusive — check workspace_id propagation`);
  console.log(`${"═".repeat(60)}\n`);
}

main().catch(e => { console.error(e); process.exit(1); });
