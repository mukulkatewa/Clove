/**
 * Seed 40-50 memory nodes + artifacts + agent defs for graph visualization experiment
 * Run: node tools/seed_memory_graph.mjs
 */

const KERNEL = "https://kernel-production-96de.up.railway.app";
const KEY = "clove-prod-70f36e07a895a89c1fd82b84ec35a4d7";

const h = { "Content-Type": "application/json", "Authorization": `Bearer ${KEY}` };
const post = (path, body) => fetch(`${KERNEL}${path}`, { method: "POST", headers: h, body: JSON.stringify(body) }).then(r => r.json());
const get  = (path)       => fetch(`${KERNEL}${path}`, { headers: h }).then(r => r.json());

// ── Agent definitions (7 agents) ────────────────────────────────────────────

const agentDefs = [
  { name: "researcher",    model: "claude-haiku-4-5", persona: "You are a deep research agent. You gather facts, cite sources, and build knowledge systematically.", tools: ["remember", "recall", "search"] },
  { name: "analyst",       model: "claude-haiku-4-5", persona: "You are a data analyst. You find patterns, draw conclusions, and synthesize research into insights.", tools: ["remember", "recall", "artifact"] },
  { name: "writer",        model: "claude-haiku-4-5", persona: "You are a technical writer. You turn analysis into clear, structured documents.", tools: ["remember", "recall", "artifact"] },
  { name: "critic",        model: "claude-haiku-4-5", persona: "You are a critical reviewer. You find flaws, gaps, and inconsistencies in plans and analyses.", tools: ["remember", "recall"] },
  { name: "planner",       model: "claude-haiku-4-5", persona: "You are a strategic planner. You break complex goals into executable steps.", tools: ["remember", "recall", "artifact"] },
  { name: "memory-keeper", model: "claude-haiku-4-5", persona: "You maintain the team's shared knowledge. You consolidate findings and keep memory clean.", tools: ["remember", "recall", "share"] },
  { name: "synthesizer",   model: "claude-haiku-4-5", persona: "You synthesize the work of all agents into a final coherent output.", tools: ["remember", "recall", "artifact"] },
];

// ── Memory nodes (45 blocks) ─────────────────────────────────────────────────

const memoryNodes = [
  // SYSTEM blocks — pinned, shared
  { name: "team-mission",       type: "system", access: "shared_read",      content: "We are building CLOVE — an AI agent OS. Our mission: make agents production-ready for businesses." },
  { name: "coding-standards",   type: "system", access: "shared_read",      content: "C++23, clang-format, no raw pointers, RAII everywhere, test everything with Catch2." },
  { name: "architecture-rules", type: "system", access: "shared_read",      content: "Kernel owns the runtime. Libs are stateless. API is the public surface. Never bypass the syscall router." },
  { name: "agent-protocols",    type: "system", access: "shared_read",      content: "Agents communicate via IPC only. No direct memory access between agents. Use share() for cross-agent memory." },

  // CORE blocks — always in context, various access
  { name: "market-research",     type: "core", access: "shared_read",      content: "Agent infra market: $6.4B VC in 2025. 46% of YC S25 batch is agent companies. Key gap: reliability and audit." },
  { name: "competitor-analysis", type: "core", access: "shared_read",      content: "Mem0: memory layer, $24M. Composio: integrations, $29M. Zep: temporal KG. Nobody has full stack kernel." },
  { name: "product-strategy",    type: "core", access: "shared_readwrite", content: "B2B focus. Agency model now. Product in May. Target: CTOs at 100-1000 person tech companies." },
  { name: "technical-moat",      type: "core", access: "shared_read",      content: "C++ kernel = 15MB binary. 86 syscalls. Runs on RPi. 300+ model routing. Full audit. Nobody else has this stack." },
  { name: "pricing-model",       type: "core", access: "shared_readwrite", content: "Agency: $30K-$150K/year. Product: usage-based + per-seat. Freemium for open source." },
  { name: "yc-application",      type: "core", access: "shared_readwrite", content: "YC target: 10 paying clients first. Revenue story + infra + agent OS angle. Apply W27." },

  // researcher agent memory
  { name: "research-gemma4",     type: "core", access: "shared_read",      content: "Gemma 4: 2B/4B edge variants, 31B dense, 26B MoE. 140 languages. Native function calling. Apache 2.0." },
  { name: "research-rpi",        type: "core", access: "shared_read",      content: "RPi 5 8GB: can run Gemma 4 E4B at ~3-5 tok/s via Ollama. Power: ~5W average. Cost: $80 device." },
  { name: "research-rag",        type: "core", access: "shared_read",      content: "GraphRAG improves multi-hop reasoning 4.5% over vector RAG. 2.3x latency. Zep uses temporal edges." },
  { name: "research-ralph-loop", type: "core", access: "shared_read",      content: "Ralph Loop: externalize completion to machine verifier. State on disk not in context. Invented 2026 by Geoffrey Huntley." },
  { name: "research-composio",   type: "core", access: "shared_read",      content: "Composio: 10K+ tools, managed auth, $29M Lightspeed. D2C/developer-facing. CLOVE is B2B complement." },

  // analyst agent memory
  { name: "analysis-gaps",       type: "core", access: "shared_readwrite", content: "CLOVE gaps: no verifier layer, no per-user isolation, no embedding search, no memory TTL." },
  { name: "analysis-strengths",  type: "core", access: "shared_readwrite", content: "CLOVE strengths: C++ speed, 86 syscalls, audit trail, sandbox, multi-model routing, MCP native." },
  { name: "analysis-timing",     type: "core", access: "shared_read",      content: "Market timing: 62% experimenting, only 23% scaling. The scaling gap IS the product." },
  { name: "analysis-cost",       type: "core", access: "shared_readwrite", content: "Per-agent cost tracking live. Budget enforcement live. Multi-model routing saves ~40% vs GPT-4o only." },
  { name: "analysis-accuracy",   type: "core", access: "shared_readwrite", content: "Accuracy gaps: self-reported completion, no confidence scoring, no step diff. Ralph Loop fixes this." },

  // planner memory
  { name: "plan-april",          type: "core", access: "shared_readwrite", content: "April: close first 3 clients via agency model. Demo: fleet + memory graph + audit. No code changes needed." },
  { name: "plan-may",            type: "core", access: "shared_readwrite", content: "May: multi-user auth (Supabase), API key generation UI, workspace isolation, verifier layer." },
  { name: "plan-hackathon",      type: "core", access: "shared_read",      content: "Kaggle Gemma 4 Good Hackathon: May 18 deadline. Build CLOVE Edge on RPi. $200K prize pool." },
  { name: "plan-yc",             type: "core", access: "shared_read",      content: "YC W27: need 10 clients, $MRR, and product live. Agency model funds the product." },
  { name: "plan-gpu",            type: "core", access: "shared_readwrite", content: "Future: CLOVE + GPU cluster + fine-tuned Gemma on workspace graph = private AI brain. Post-YC play." },

  // RECALL blocks — on demand
  { name: "railway-urls",        type: "recall", access: "shared_read",    content: "Kernel: https://kernel-production-96de.up.railway.app | MCP: https://mcp-production-07a6.up.railway.app/mcp" },
  { name: "npm-package",         type: "recall", access: "shared_read",    content: "npm: @cloveos/mcp-server v2.0.2 | @cloveos/cli v0.1.1. Published. CLOVE_KERNEL_URL + CLOVE_API_KEY env vars." },
  { name: "api-key-prod",        type: "recall", access: "private",        content: "CLOVE_API_KEY: clove-prod-70f36e07a895a89c1fd82b84ec35a4d7 | MCP key: clove-mcp-860c9491716257031fabe61007930b91" },
  { name: "supabase-config",     type: "recall", access: "shared_read",    content: "Supabase: pzldqapdbiszeumueyzh.supabase.co. Tables: workspaces, agent_definitions, agent_runs, swarms." },
  { name: "docker-config",       type: "recall", access: "shared_read",    content: "deploy/Dockerfile: Alpine 3.19, two-stage. ENV PORT=8080. CMD: clove_kernel --api --openrouter --mcp --a2a" },

  { name: "syscall-map",         type: "recall", access: "shared_read",    content: "86 syscalls across 22 modules: CORE, AGENTS, IPC, STATE, PERMISSIONS, NETWORK, EVENTS, REPLAY, ASYNC, WORLDS, TUNNELS, METRICS, INTEGRATIONS, CONTEXT, MEMORY, META" },
  { name: "memory-tiers",        type: "recall", access: "shared_read",    content: "Tier 1: LLM context (assembled). Tier 2: kernel RAM (<1ms). Tier 3: SQLite WAL (1-10ms). Boot-loaded from Tier 3 to Tier 2." },
  { name: "sandbox-linux",       type: "recall", access: "shared_read",    content: "Linux sandbox: PID/MNT/UTS/NET namespaces, cgroups v2, Landlock filesystem, seccomp BPF (27 blocked syscalls)." },
  { name: "mcp-tools-count",     type: "recall", access: "shared_read",    content: "79 live MCP tools: run, fleet, jobs, agents, daemons, swarms, memory, audit, governance, worlds, MCP passthrough." },
  { name: "ros2-notes",          type: "recall", access: "shared_read",    content: "ROS2 integration: MCP bridge wrapping ROS2 node, or HTTP syscall to rosbridge_server. C++ on both sides = clean." },

  { name: "graph-view-plan",     type: "recall", access: "shared_readwrite", content: "Memory graph visualization: nodes=artifacts+memory+agents+chains. Edges=parent_ids+authored+owns+shared. D3.js force layout." },
  { name: "ralph-loop-impl",     type: "recall", access: "shared_readwrite", content: "Ralph Loop in CLOVE: add post-run verifier that re-queues job if objective criteria not met. 3 days C++ work." },
  { name: "multi-user-plan",     type: "recall", access: "shared_read",    content: "Multi-user: Supabase Auth + api_keys table + kernel KeyCache (60s TTL) + workspace_id scoping on all endpoints." },
  { name: "edge-install",        type: "recall", access: "shared_read",    content: "Edge install: curl install.clove.sh | bash. Auto-installs Ollama + Gemma 4 E2B + CLOVE kernel ARM binary." },
  { name: "b2b-pitch",           type: "recall", access: "shared_read",    content: "Pitch: We don't replace your agents. We make them production-ready. Memory. Audit. Scale. Cost control. One MCP URL." },

  { name: "context-graph-thesis",type: "recall", access: "shared_read",    content: "Foundation Capital Dec 2025: context graphs = next trillion-dollar platform. Decision traces + time-aware edges = moat." },
  { name: "agentic-stats",       type: "recall", access: "shared_read",    content: "$6.42B VC into agents in 2025. Goldman: 60% of software profits to agents by 2030. 40% of YC S25 = agent companies." },
  { name: "icp-profile",         type: "recall", access: "shared_read",    content: "ICP: 100-1000 person tech company, already running agents in prod, CTO makes the call. $30K-$150K/year." },
  { name: "accuracy-thesis",     type: "recall", access: "shared_readwrite", content: "Accuracy > cost > speed. One wrong agent action costs more than a year of API bills. Audit + replay + verifier = reliability." },
  { name: "robots-ros2",         type: "recall", access: "shared_readwrite", content: "Robotics angle: CLOVE kernel as robot brain runtime. ROS2 bridge. Physical Intelligence, Figure, Sanctuary moving toward LLM agents." },
];

// ── Seed everything ──────────────────────────────────────────────────────────

async function seed() {
  console.log("🌱 Seeding agent definitions...");
  const agentResults = [];
  for (const a of agentDefs) {
    const r = await post("/api/agent-defs", a);
    agentResults.push(r);
    console.log(`  ✓ agent: ${a.name}`);
  }

  console.log("\n🌱 Seeding memory blocks...");
  const memResults = [];
  for (const m of memoryNodes) {
    const r = await post("/api/memory", m);
    memResults.push(r);
    console.log(`  ✓ memory: ${m.name} [${m.type}/${m.access}]`);
  }

  console.log("\n✅ Done.");
  console.log(`   ${agentResults.length} agents`);
  console.log(`   ${memResults.length} memory blocks`);
  console.log("\n👉 Open tools/memory_graph.html in your browser");
}

seed().catch(console.error);
