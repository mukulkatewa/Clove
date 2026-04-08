/**
 * CLOVE live stack test
 * Run: node tools/test_live.mjs
 * Tests: health → memory → job submit → poll → audit → cost
 */

const KERNEL = "https://kernel-production-96de.up.railway.app";
const KEY    = "clove-prod-70f36e07a895a89c1fd82b84ec35a4d7";
const h      = { "Content-Type": "application/json", "Authorization": `Bearer ${KEY}` };

const get  = (path)       => fetch(`${KERNEL}${path}`, { headers: h }).then(r => r.json());
const post = (path, body) => fetch(`${KERNEL}${path}`, { method: "POST", headers: h, body: JSON.stringify(body) }).then(r => r.json());

let passed = 0, failed = 0;

function ok(label, val) {
  console.log(`  ✓  ${label}`);
  if (val !== undefined) console.log(`     → ${JSON.stringify(val)}`);
  passed++;
}

function fail(label, reason) {
  console.log(`  ✗  ${label}`);
  console.log(`     → ${reason}`);
  failed++;
}

function section(title) {
  console.log(`\n── ${title} ${"─".repeat(50 - title.length)}`);
}

async function sleep(ms) {
  return new Promise(r => setTimeout(r, ms));
}

async function poll(jobId, maxWait = 30000) {
  const start = Date.now();
  while (Date.now() - start < maxWait) {
    const r = await get(`/api/jobs/${jobId}`);
    const status = r.status || r.job?.status;
    process.stdout.write(`\r     polling… status=${status}   `);
    if (status === "completed" || status === "failed") {
      process.stdout.write("\n");
      return r;
    }
    await sleep(1500);
  }
  process.stdout.write("\n");
  return null;
}

// ─────────────────────────────────────────────────────────────────────────────

async function run() {
  console.log("CLOVE Live Stack Test");
  console.log("=".repeat(55));

  // ── 1. Health ──────────────────────────────────────────────────────────────
  section("1. Health");
  try {
    const r = await get("/api/health");
    if (r.status === "ok") ok("kernel alive", `uptime=${r.uptime_s}s syscalls=${r.syscall_count}`);
    else fail("kernel health", JSON.stringify(r));
  } catch (e) { fail("kernel reachable", e.message); }

  // ── 2. Memory write + read ─────────────────────────────────────────────────
  section("2. Memory — write + read");
  const memName = `test-${Date.now()}`;
  try {
    const w = await post("/api/memory", {
      name: memName, type: "core", access: "shared_read",
      content: "live test block written at " + new Date().toISOString(),
    });
    if (w.id || w.name) ok("memory write", `id=${w.id || w.name}`);
    else fail("memory write", JSON.stringify(w));
  } catch (e) { fail("memory write", e.message); }

  try {
    const list = await get("/api/memory");
    const blocks = list.blocks || list.memories || list;
    const found = Array.isArray(blocks) && blocks.find(b => b.name === memName);
    if (found) ok("memory read back", `content="${found.content?.slice(0,40)}…"`);
    else fail("memory read back", `not found in ${blocks.length} blocks`);
  } catch (e) { fail("memory read", e.message); }

  // ── 3. Agent defs ─────────────────────────────────────────────────────────
  section("3. Agent definitions");
  try {
    const r = await get("/api/agent-defs");
    const agents = r.agent_defs || r.agents || r;
    if (Array.isArray(agents) && agents.length > 0)
      ok(`${agents.length} agent defs loaded`, agents.map(a => a.name).join(", "));
    else fail("agent defs", JSON.stringify(r));
  } catch (e) { fail("agent defs", e.message); }

  // ── 4. Job submit + poll ───────────────────────────────────────────────────
  section("4. Job — submit + run (needs OpenRouter key)");
  let jobId = null;
  try {
    const r = await post("/api/jobs", {
      goal: "Store a memory block named clove-live-test with content: stack test passed. Then return done.",
      agent_name: "researcher",
      workspace_id: "test-live",
      model: "openai/gpt-4o-mini",   // cheap OpenRouter model
      budget_usd: 0.05,
      max_steps: 6,
      allowed_tools: ["remember", "recall"],
    });

    if (r.error) {
      fail("job submit", r.error);
    } else {
      jobId = r.job_id || r.id;
      ok("job submitted", `id=${jobId}`);
    }
  } catch (e) { fail("job submit", e.message); }

  if (jobId) {
    console.log(`     waiting up to 30s for job to complete…`);
    const result = await poll(jobId);
    if (!result) {
      fail("job completed", "timed out after 30s");
    } else {
      const status = result.status || result.job?.status;
      const cost   = result.cost_usd || result.job?.cost_usd;
      const steps  = result.steps_used || result.job?.steps_used;
      if (status === "completed") {
        ok("job completed", `steps=${steps} cost=$${cost?.toFixed(5) || "?"}`);
      } else {
        fail("job completed", `status=${status} error=${result.error || result.job?.error || "?"}`);
      }
    }
  }

  // ── 5. Audit log ──────────────────────────────────────────────────────────
  section("5. Audit trail");
  try {
    const r = await get("/api/audit");
    const entries = Array.isArray(r) ? r : (r.entries || []);
    if (entries.length > 0)
      ok(`${entries.length} audit entries`, `latest: ${entries[0]?.category || entries[0]?.event || JSON.stringify(entries[0]).slice(0,60)}`);
    else
      ok("audit endpoint live", "0 entries (job hasn't run yet or audit not populated)");
  } catch (e) { fail("audit", e.message); }

  // ── 6. Runs history ───────────────────────────────────────────────────────
  section("6. Run history");
  try {
    const r = await get("/api/runs");
    const runs = Array.isArray(r) ? r : (r.runs || []);
    if (runs.length > 0)
      ok(`${runs.length} runs in history`, `latest: agent=${runs[0]?.agent_name} status=${runs[0]?.status}`);
    else
      ok("runs endpoint live", "0 runs yet");
  } catch (e) { fail("runs", e.message); }

  // ── 7. Metrics / cost tracking ────────────────────────────────────────────
  section("7. Cost tracking");
  try {
    const r = await get("/api/metrics");
    ok("metrics endpoint", `cost=$${r.llm?.current_cost_usd?.toFixed(5)||0} agents=${r.agent_count||0} audit=${r.audit_entries||0}`);
  } catch (e) { fail("metrics", e.message); }

  // ── Summary ───────────────────────────────────────────────────────────────
  console.log("\n" + "=".repeat(55));
  console.log(`  ${passed} passed  ${failed} failed`);

  if (failed === 0) {
    console.log("  Full stack is live. Ship it.");
  } else if (passed >= 5) {
    console.log("  Core is working. Job runner needs OpenRouter key on Railway.");
    console.log("  Fix: railway service --service kernel && railway variables set OPENROUTER_API_KEY=sk-or-v1-...");
  } else {
    console.log("  Something deeper is broken. Check Railway logs.");
    console.log("  Debug: railway logs --service kernel");
  }
  console.log("=".repeat(55));
}

run().catch(console.error);
