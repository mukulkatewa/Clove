/**
 * Synchronous-style client for the CLOVE kernel over Unix domain sockets.
 *
 * Uses Node.js `net` module with promise-based wrappers around the
 * stream-oriented socket for a clean async/await API.
 */

import { createConnection, Socket } from "node:net";
import {
  HEADER_SIZE,
  SyscallOp,
  packMessage,
  unpackHeader,
} from "./protocol.js";
import type {
  AgentInfo,
  AgentMetricsResponse,
  Artifact,
  AuditLogResponse,
  BudgetResponse,
  Chain,
  ContextAssemblyResponse,
  CredsResponse,
  ExecResponse,
  HelloResponse,
  HttpResponse,
  KernelResponse,
  LlmConfigResponse,
  LlmReportResponse,
  McpListResponse,
  MemoryBlock,
  Message,
  Event,
  OtelSpanResponse,
  Permissions,
  PiiRedactResponse,
  PiiScanResponse,
  PriorityResponse,
  ReadFileResponse,
  RecordingStatusResponse,
  SpawnResponse,
  SystemMetricsResponse,
  ThinkResponse,
  TunnelResponse,
  World,
} from "./types.js";

/**
 * Low-level CLOVE kernel client.
 *
 * Communicates over a Unix domain socket using the binary IPC protocol
 * (17-byte header + JSON payload). All public methods return Promises.
 */
export class CloveClient {
  readonly socketPath: string;
  readonly agentId: number;
  private socket: Socket | null = null;
  private buffer: Buffer = Buffer.alloc(0);
  private pendingResolve: ((data: Buffer) => void) | null = null;
  private pendingBytes = 0;

  constructor(socketPath: string = "/tmp/clove.sock", agentId: number = 0) {
    this.socketPath = socketPath;
    this.agentId = agentId;
  }

  // ── Connection lifecycle ──────────────────────────────────────

  /**
   * Open the Unix socket connection.
   * @returns Resolves to `true` on successful connection.
   */
  connect(): Promise<boolean> {
    return new Promise((resolve, reject) => {
      this.socket = createConnection({ path: this.socketPath }, () => {
        resolve(true);
      });

      this.socket.on("data", (chunk: Buffer) => {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        this.tryFlush();
      });

      this.socket.on("error", (err) => {
        if (this.pendingResolve) {
          // Surface the error to the waiting _recvExact call
          this.pendingResolve = null;
          this.pendingBytes = 0;
        }
        reject(err);
      });

      this.socket.on("close", () => {
        if (this.pendingResolve) {
          this.pendingResolve = null;
          this.pendingBytes = 0;
        }
      });
    });
  }

  /** Close the socket if open. */
  disconnect(): void {
    if (this.socket !== null) {
      try {
        this.socket.destroy();
      } catch {
        // ignore
      }
      this.socket = null;
    }
    this.buffer = Buffer.alloc(0);
    this.pendingResolve = null;
    this.pendingBytes = 0;
  }

  // ── Internal transport ────────────────────────────────────────

  private tryFlush(): void {
    if (
      this.pendingResolve &&
      this.pendingBytes > 0 &&
      this.buffer.length >= this.pendingBytes
    ) {
      const data = this.buffer.subarray(0, this.pendingBytes);
      this.buffer = this.buffer.subarray(this.pendingBytes);
      const resolve = this.pendingResolve;
      this.pendingResolve = null;
      this.pendingBytes = 0;
      resolve(Buffer.from(data));
    }
  }

  /**
   * Read exactly `n` bytes from the socket.
   */
  private _recvExact(n: number): Promise<Buffer> {
    if (this.buffer.length >= n) {
      const data = this.buffer.subarray(0, n);
      this.buffer = this.buffer.subarray(n);
      return Promise.resolve(Buffer.from(data));
    }

    return new Promise<Buffer>((resolve, reject) => {
      if (!this.socket || this.socket.destroyed) {
        reject(new Error("Socket closed while reading"));
        return;
      }
      this.pendingResolve = resolve;
      this.pendingBytes = n;

      // Handle socket close while waiting
      const onClose = () => {
        if (this.pendingResolve === resolve) {
          this.pendingResolve = null;
          this.pendingBytes = 0;
          reject(new Error("Socket closed while reading"));
        }
      };
      const onError = (err: Error) => {
        if (this.pendingResolve === resolve) {
          this.pendingResolve = null;
          this.pendingBytes = 0;
          reject(err);
        }
      };
      this.socket.once("close", onClose);
      this.socket.once("error", onError);
    });
  }

  /**
   * Send a syscall and wait for the kernel's response.
   */
  private async _send(
    opcode: number,
    payload?: Record<string, unknown> | null,
  ): Promise<Record<string, unknown>> {
    if (!this.socket || this.socket.destroyed) {
      throw new Error("Not connected — call connect() first");
    }

    const payloadStr = payload ? JSON.stringify(payload) : "";
    const msg = packMessage(this.agentId, opcode, payloadStr);

    await new Promise<void>((resolve, reject) => {
      this.socket!.write(msg, (err) => {
        if (err) reject(err);
        else resolve();
      });
    });

    // Read the 17-byte response header
    const headerData = await this._recvExact(HEADER_SIZE);
    const { payloadSize } = unpackHeader(headerData);

    // Read the response payload
    if (payloadSize > 0) {
      const raw = await this._recvExact(payloadSize);
      return JSON.parse(raw.toString("utf-8")) as Record<string, unknown>;
    }
    return {};
  }

  // ── Core ──────────────────────────────────────────────────────

  async hello(): Promise<HelloResponse> {
    return this._send(SyscallOp.SYS_HELLO);
  }

  async think(prompt: string): Promise<ThinkResponse> {
    return this._send(SyscallOp.SYS_THINK, { prompt });
  }

  async thinkWithContext(
    prompt: string,
    chainId: string,
    model?: string,
  ): Promise<ThinkResponse> {
    const payload: Record<string, unknown> = {
      prompt,
      chain_id: chainId,
      auto_context: true,
    };
    if (model) payload.model = model;
    return this._send(SyscallOp.SYS_THINK, payload);
  }

  // ── Agents ────────────────────────────────────────────────────

  async spawn(name: string, command: string): Promise<SpawnResponse> {
    return this._send(SyscallOp.SYS_SPAWN, { name, command });
  }

  async kill(agentId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_KILL, { agent_id: agentId });
  }

  async listAgents(): Promise<AgentInfo[]> {
    const resp = await this._send(SyscallOp.SYS_LIST);
    return (resp.agents as AgentInfo[]) ?? [];
  }

  async pause(agentId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_PAUSE, { agent_id: agentId });
  }

  async resume(agentId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_RESUME, { agent_id: agentId });
  }

  // ── IPC ───────────────────────────────────────────────────────

  async sendMessage(targetId: number, content: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_SEND, {
      target_id: targetId,
      content,
    });
  }

  async recvMessages(maxCount: number = 50): Promise<Message[]> {
    const resp = await this._send(SyscallOp.SYS_RECV, { max_count: maxCount });
    return (resp.messages as Message[]) ?? [];
  }

  async registerName(name: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_REGISTER, { name });
  }

  // ── Events ────────────────────────────────────────────────────

  async subscribe(eventType: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_SUBSCRIBE, { event_type: eventType });
  }

  async unsubscribe(eventType: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_UNSUBSCRIBE, { event_type: eventType });
  }

  async pollEvents(maxEvents: number = 50): Promise<Event[]> {
    const resp = await this._send(SyscallOp.SYS_POLL_EVENTS, {
      max_events: maxEvents,
    });
    return (resp.events as Event[]) ?? [];
  }

  async emit(
    eventType: string,
    data?: Record<string, unknown>,
  ): Promise<KernelResponse> {
    const payload: Record<string, unknown> = { event_type: eventType };
    if (data !== undefined) payload.data = data;
    return this._send(SyscallOp.SYS_EMIT, payload);
  }

  // ── State ─────────────────────────────────────────────────────

  async store(
    key: string,
    value: unknown,
    scope: string = "global",
    ttlMs: number = 0,
  ): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_STORE, {
      key,
      value,
      scope,
      ttl_ms: ttlMs,
    });
  }

  async fetch(key: string): Promise<unknown> {
    const resp = await this._send(SyscallOp.SYS_FETCH, { key });
    return resp.value;
  }

  async delete(key: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_DELETE, { key });
  }

  async keys(prefix: string = ""): Promise<string[]> {
    const resp = await this._send(SyscallOp.SYS_KEYS, { prefix });
    return (resp.keys as string[]) ?? [];
  }

  // ── Permissions ───────────────────────────────────────────────

  async getPermissions(agentId?: number): Promise<KernelResponse> {
    const payload: Record<string, unknown> = {};
    if (agentId !== undefined) payload.agent_id = agentId;
    return this._send(
      SyscallOp.SYS_GET_PERMS,
      Object.keys(payload).length > 0 ? payload : null,
    );
  }

  async setPermissions(
    permissions: Permissions,
    agentId?: number,
  ): Promise<KernelResponse> {
    const payload: Record<string, unknown> = { permissions };
    if (agentId !== undefined) payload.agent_id = agentId;
    return this._send(SyscallOp.SYS_SET_PERMS, payload);
  }

  // ── HTTP ──────────────────────────────────────────────────────

  async http(
    url: string,
    method: string = "GET",
    body?: string,
    headers?: Record<string, string>,
  ): Promise<HttpResponse> {
    const payload: Record<string, unknown> = { url, method };
    if (body !== undefined) payload.body = body;
    if (headers !== undefined) payload.headers = headers;
    return this._send(SyscallOp.SYS_HTTP, payload);
  }

  // ── File I/O ──────────────────────────────────────────────────

  async exec(command: string, args?: string[]): Promise<ExecResponse> {
    const payload: Record<string, unknown> = { command };
    if (args !== undefined) payload.args = args;
    return this._send(SyscallOp.SYS_EXEC, payload);
  }

  async read(path: string): Promise<ReadFileResponse> {
    return this._send(SyscallOp.SYS_READ, { path });
  }

  async write(path: string, content: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WRITE, { path, content });
  }

  // ── Memory Blocks ─────────────────────────────────────────────

  async memCreate(
    name: string,
    type: string = "core",
    access: string = "private",
    content: string = "",
    maxTokens: number = 0,
  ): Promise<MemoryBlock> {
    const payload: Record<string, unknown> = { name, type, access, content };
    if (maxTokens > 0) payload.max_tokens = maxTokens;
    return this._send(SyscallOp.SYS_MEM_CREATE, payload);
  }

  async memRead(id?: string, name?: string): Promise<MemoryBlock> {
    const payload: Record<string, unknown> = {};
    if (id) payload.id = id;
    if (name) payload.name = name;
    return this._send(SyscallOp.SYS_MEM_READ, payload);
  }

  async memWrite(id: string, content: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_MEM_WRITE, { id, content });
  }

  async memAppend(id: string, content: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_MEM_APPEND, { id, content });
  }

  async memDelete(id: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_MEM_DELETE, { id });
  }

  async memList(limit: number = 100): Promise<MemoryBlock[]> {
    const resp = await this._send(SyscallOp.SYS_MEM_LIST, { limit });
    return (resp.blocks as MemoryBlock[]) ?? [];
  }

  async memShare(id: string, targetAgentId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_MEM_SHARE, {
      id,
      target_agent_id: targetAgentId,
    });
  }

  // ── Context / Artifacts ───────────────────────────────────────

  async docCreate(
    type: string,
    title: string,
    content: string,
    chainId: string,
    parentIds?: string[],
    metadata?: Record<string, unknown>,
  ): Promise<Artifact> {
    const payload: Record<string, unknown> = {
      type,
      title,
      content,
      chain_id: chainId,
    };
    if (parentIds) payload.parent_ids = parentIds;
    if (metadata) payload.metadata = metadata;
    return this._send(SyscallOp.SYS_DOC_CREATE, payload);
  }

  async docRead(id: string): Promise<Artifact> {
    return this._send(SyscallOp.SYS_DOC_READ, { id });
  }

  async docUpdate(
    id: string,
    content?: string,
    state?: string,
  ): Promise<KernelResponse> {
    const payload: Record<string, unknown> = { id };
    if (content !== undefined) payload.content = content;
    if (state !== undefined) payload.state = state;
    return this._send(SyscallOp.SYS_DOC_UPDATE, payload);
  }

  async docList(
    chainId?: string,
    type?: string,
    state?: string,
    limit: number = 100,
  ): Promise<Artifact[]> {
    const payload: Record<string, unknown> = { limit };
    if (chainId) payload.chain_id = chainId;
    if (type) payload.type = type;
    if (state) payload.state = state;
    const resp = await this._send(SyscallOp.SYS_DOC_LIST, payload);
    return (resp.artifacts as Artifact[]) ?? [];
  }

  async docDelete(id: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_DOC_DELETE, { id });
  }

  async chainCreate(
    name: string,
    description: string = "",
    metadata?: Record<string, unknown>,
  ): Promise<Chain> {
    const payload: Record<string, unknown> = { name, description };
    if (metadata) payload.metadata = metadata;
    return this._send(SyscallOp.SYS_CHAIN_CREATE, payload);
  }

  async chainGet(id: string): Promise<Chain> {
    return this._send(SyscallOp.SYS_CHAIN_GET, { id });
  }

  async chainFork(chainId: string, atArtifactId: string): Promise<Chain> {
    return this._send(SyscallOp.SYS_CHAIN_FORK, {
      chain_id: chainId,
      at_artifact_id: atArtifactId,
    });
  }

  async contextAssemble(
    chainId: string,
    maxTokens: number = 128_000,
  ): Promise<ContextAssemblyResponse> {
    return this._send(SyscallOp.SYS_CONTEXT_ASSEMBLE, {
      chain_id: chainId,
      max_tokens: maxTokens,
    });
  }

  // ── Budget ────────────────────────────────────────────────────

  async setBudget(options: {
    agentId?: number;
    maxTokens?: number;
    maxSteps?: number;
    maxTimeMs?: number;
    maxCostUsd?: number;
    killOnExceeded?: boolean;
    reset?: boolean;
  } = {}): Promise<BudgetResponse> {
    const payload: Record<string, unknown> = {};
    if (options.agentId !== undefined) payload.agent_id = options.agentId;
    if (options.maxTokens !== undefined) payload.max_tokens = options.maxTokens;
    if (options.maxSteps !== undefined) payload.max_steps = options.maxSteps;
    if (options.maxTimeMs !== undefined) payload.max_time_ms = options.maxTimeMs;
    if (options.maxCostUsd !== undefined) payload.max_cost_usd = options.maxCostUsd;
    if (options.killOnExceeded) payload.kill_on_exceeded = true;
    if (options.reset) payload.reset = true;
    return this._send(SyscallOp.SYS_SET_BUDGET, payload);
  }

  async getBudget(agentId?: number): Promise<BudgetResponse> {
    const payload: Record<string, unknown> = {};
    if (agentId !== undefined) payload.agent_id = agentId;
    return this._send(
      SyscallOp.SYS_GET_BUDGET,
      Object.keys(payload).length > 0 ? payload : null,
    );
  }

  // ── Priority / Scheduling ────────────────────────────────────

  async setPriority(
    priority: string = "normal",
    agentId?: number,
  ): Promise<PriorityResponse> {
    const payload: Record<string, unknown> = { priority };
    if (agentId !== undefined) payload.agent_id = agentId;
    return this._send(SyscallOp.SYS_SET_PRIORITY, payload);
  }

  async getPriority(agentId?: number): Promise<PriorityResponse> {
    const payload: Record<string, unknown> = {};
    if (agentId !== undefined) payload.agent_id = agentId;
    return this._send(
      SyscallOp.SYS_GET_PRIORITY,
      Object.keys(payload).length > 0 ? payload : null,
    );
  }

  // ── MCP ───────────────────────────────────────────────────────

  async mcpCall(
    server: string,
    tool: string,
    args?: Record<string, unknown>,
  ): Promise<KernelResponse> {
    const payload: Record<string, unknown> = { server, tool };
    if (args !== undefined) payload.arguments = args;
    return this._send(SyscallOp.SYS_MCP_CALL, payload);
  }

  async mcpList(): Promise<McpListResponse> {
    return this._send(SyscallOp.SYS_MCP_LIST);
  }

  // ── PII ───────────────────────────────────────────────────────

  async piiScan(text: string): Promise<PiiScanResponse> {
    return this._send(SyscallOp.SYS_PII_SCAN, { text });
  }

  async piiRedact(text: string): Promise<PiiRedactResponse> {
    return this._send(SyscallOp.SYS_PII_REDACT, { text });
  }

  // ── Metrics ───────────────────────────────────────────────────

  async metricsSystem(): Promise<SystemMetricsResponse> {
    return this._send(SyscallOp.SYS_METRICS_SYSTEM);
  }

  async metricsAgent(agentId?: number): Promise<AgentMetricsResponse> {
    const payload: Record<string, unknown> = {};
    if (agentId !== undefined) payload.agent_id = agentId;
    return this._send(
      SyscallOp.SYS_METRICS_AGENT,
      Object.keys(payload).length > 0 ? payload : null,
    );
  }

  async metricsAllAgents(): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_METRICS_ALL_AGENTS);
  }

  async metricsCgroup(): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_METRICS_CGROUP);
  }

  // ── Worlds ────────────────────────────────────────────────────

  async worldCreate(
    name: string,
    metadata?: Record<string, unknown>,
  ): Promise<KernelResponse> {
    const payload: Record<string, unknown> = { name };
    if (metadata !== undefined) payload.metadata = metadata;
    return this._send(SyscallOp.SYS_WORLD_CREATE, payload);
  }

  async worldDestroy(worldId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WORLD_DESTROY, { world_id: worldId });
  }

  async worldList(): Promise<World[]> {
    const resp = await this._send(SyscallOp.SYS_WORLD_LIST);
    return (resp.worlds as World[]) ?? [];
  }

  async worldJoin(worldId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WORLD_JOIN, { world_id: worldId });
  }

  async worldLeave(worldId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WORLD_LEAVE, { world_id: worldId });
  }

  async worldEvent(
    worldId: number,
    eventType: string,
    data?: Record<string, unknown>,
  ): Promise<KernelResponse> {
    const payload: Record<string, unknown> = {
      world_id: worldId,
      event_type: eventType,
    };
    if (data !== undefined) payload.data = data;
    return this._send(SyscallOp.SYS_WORLD_EVENT, payload);
  }

  async worldState(worldId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WORLD_STATE, { world_id: worldId });
  }

  async worldSnapshot(worldId: number): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WORLD_SNAPSHOT, { world_id: worldId });
  }

  async worldRestore(worldId: number, snapshotId: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_WORLD_RESTORE, {
      world_id: worldId,
      snapshot_id: snapshotId,
    });
  }

  // ── Tunnels ───────────────────────────────────────────────────

  async tunnelConnect(
    remoteUrl: string,
    config?: Record<string, unknown>,
  ): Promise<TunnelResponse> {
    const payload: Record<string, unknown> = { remote_url: remoteUrl };
    if (config !== undefined) payload.config = config;
    return this._send(SyscallOp.SYS_TUNNEL_CONNECT, payload);
  }

  async tunnelDisconnect(tunnelId: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_TUNNEL_DISCONNECT, {
      tunnel_id: tunnelId,
    });
  }

  async tunnelStatus(tunnelId: string): Promise<TunnelResponse> {
    return this._send(SyscallOp.SYS_TUNNEL_STATUS, { tunnel_id: tunnelId });
  }

  async tunnelListRemotes(): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_TUNNEL_LIST_REMOTES);
  }

  async tunnelConfig(
    tunnelId: string,
    config: Record<string, unknown>,
  ): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_TUNNEL_CONFIG, {
      tunnel_id: tunnelId,
      config,
    });
  }

  // ── Recording / Replay ────────────────────────────────────────

  async recordStart(maxEntries: number = 50_000): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_RECORD_START, {
      max_entries: maxEntries,
    });
  }

  async recordStop(): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_RECORD_STOP);
  }

  async recordStatus(): Promise<RecordingStatusResponse> {
    return this._send(SyscallOp.SYS_RECORD_STATUS);
  }

  async replayStart(): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_REPLAY_START);
  }

  async replayStatus(): Promise<RecordingStatusResponse> {
    return this._send(SyscallOp.SYS_REPLAY_STATUS);
  }

  // ── Audit ─────────────────────────────────────────────────────

  async getAuditLog(
    category?: string,
    limit: number = 100,
  ): Promise<AuditLogResponse> {
    const payload: Record<string, unknown> = { limit };
    if (category !== undefined) payload.category = category;
    return this._send(SyscallOp.SYS_GET_AUDIT_LOG, payload);
  }

  async setAuditConfig(
    config: Record<string, unknown>,
  ): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_SET_AUDIT_CONFIG, config);
  }

  // ── A2A ───────────────────────────────────────────────────────

  async a2aSend(
    targetUrl: string,
    message: Record<string, unknown>,
  ): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_A2A_SEND, {
      target_url: targetUrl,
      message,
    });
  }

  async a2aRecv(maxCount: number = 50): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_A2A_RECV, { max_count: maxCount });
  }

  // ── Credentials ───────────────────────────────────────────────

  async credsGet(name: string): Promise<CredsResponse> {
    return this._send(SyscallOp.SYS_CREDS_GET, { name });
  }

  // ── OTel ──────────────────────────────────────────────────────

  async otelSpan(
    name: string,
    attributes?: Record<string, unknown>,
  ): Promise<OtelSpanResponse> {
    const payload: Record<string, unknown> = { name };
    if (attributes !== undefined) payload.attributes = attributes;
    return this._send(SyscallOp.SYS_OTEL_SPAN, payload);
  }

  // ── LLM ───────────────────────────────────────────────────────

  async llmConfig(): Promise<LlmConfigResponse> {
    return this._send(SyscallOp.SYS_LLM_CONFIG);
  }

  async llmReport(): Promise<LlmReportResponse> {
    return this._send(SyscallOp.SYS_LLM_REPORT);
  }

  // ── Async ─────────────────────────────────────────────────────

  async asyncPoll(taskId: string): Promise<KernelResponse> {
    return this._send(SyscallOp.SYS_ASYNC_POLL, { task_id: taskId });
  }
}
