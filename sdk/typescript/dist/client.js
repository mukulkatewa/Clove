/**
 * Synchronous-style client for the CLOVE kernel over Unix domain sockets.
 *
 * Uses Node.js `net` module with promise-based wrappers around the
 * stream-oriented socket for a clean async/await API.
 */
import { createConnection } from "node:net";
import { HEADER_SIZE, SyscallOp, packMessage, unpackHeader, } from "./protocol.js";
/**
 * Low-level CLOVE kernel client.
 *
 * Communicates over a Unix domain socket using the binary IPC protocol
 * (17-byte header + JSON payload). All public methods return Promises.
 */
export class CloveClient {
    socketPath;
    agentId;
    socket = null;
    buffer = Buffer.alloc(0);
    pendingResolve = null;
    pendingBytes = 0;
    constructor(socketPath = "/tmp/clove.sock", agentId = 0) {
        this.socketPath = socketPath;
        this.agentId = agentId;
    }
    // ── Connection lifecycle ──────────────────────────────────────
    /**
     * Open the Unix socket connection.
     * @returns Resolves to `true` on successful connection.
     */
    connect() {
        return new Promise((resolve, reject) => {
            this.socket = createConnection({ path: this.socketPath }, () => {
                resolve(true);
            });
            this.socket.on("data", (chunk) => {
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
    disconnect() {
        if (this.socket !== null) {
            try {
                this.socket.destroy();
            }
            catch {
                // ignore
            }
            this.socket = null;
        }
        this.buffer = Buffer.alloc(0);
        this.pendingResolve = null;
        this.pendingBytes = 0;
    }
    // ── Internal transport ────────────────────────────────────────
    tryFlush() {
        if (this.pendingResolve &&
            this.pendingBytes > 0 &&
            this.buffer.length >= this.pendingBytes) {
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
    _recvExact(n) {
        if (this.buffer.length >= n) {
            const data = this.buffer.subarray(0, n);
            this.buffer = this.buffer.subarray(n);
            return Promise.resolve(Buffer.from(data));
        }
        return new Promise((resolve, reject) => {
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
            const onError = (err) => {
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
    async _send(opcode, payload) {
        if (!this.socket || this.socket.destroyed) {
            throw new Error("Not connected — call connect() first");
        }
        const payloadStr = payload ? JSON.stringify(payload) : "";
        const msg = packMessage(this.agentId, opcode, payloadStr);
        await new Promise((resolve, reject) => {
            this.socket.write(msg, (err) => {
                if (err)
                    reject(err);
                else
                    resolve();
            });
        });
        // Read the 17-byte response header
        const headerData = await this._recvExact(HEADER_SIZE);
        const { payloadSize } = unpackHeader(headerData);
        // Read the response payload
        if (payloadSize > 0) {
            const raw = await this._recvExact(payloadSize);
            return JSON.parse(raw.toString("utf-8"));
        }
        return {};
    }
    // ── Core ──────────────────────────────────────────────────────
    async hello() {
        return this._send(SyscallOp.SYS_HELLO);
    }
    async think(prompt) {
        return this._send(SyscallOp.SYS_THINK, { prompt });
    }
    async thinkWithContext(prompt, chainId, model) {
        const payload = {
            prompt,
            chain_id: chainId,
            auto_context: true,
        };
        if (model)
            payload.model = model;
        return this._send(SyscallOp.SYS_THINK, payload);
    }
    // ── Agents ────────────────────────────────────────────────────
    async spawn(name, command) {
        return this._send(SyscallOp.SYS_SPAWN, { name, command });
    }
    async kill(agentId) {
        return this._send(SyscallOp.SYS_KILL, { agent_id: agentId });
    }
    async listAgents() {
        const resp = await this._send(SyscallOp.SYS_LIST);
        return resp.agents ?? [];
    }
    async pause(agentId) {
        return this._send(SyscallOp.SYS_PAUSE, { agent_id: agentId });
    }
    async resume(agentId) {
        return this._send(SyscallOp.SYS_RESUME, { agent_id: agentId });
    }
    // ── IPC ───────────────────────────────────────────────────────
    async sendMessage(targetId, content) {
        return this._send(SyscallOp.SYS_SEND, {
            target_id: targetId,
            content,
        });
    }
    async recvMessages(maxCount = 50) {
        const resp = await this._send(SyscallOp.SYS_RECV, { max_count: maxCount });
        return resp.messages ?? [];
    }
    async registerName(name) {
        return this._send(SyscallOp.SYS_REGISTER, { name });
    }
    // ── Events ────────────────────────────────────────────────────
    async subscribe(eventType) {
        return this._send(SyscallOp.SYS_SUBSCRIBE, { event_type: eventType });
    }
    async unsubscribe(eventType) {
        return this._send(SyscallOp.SYS_UNSUBSCRIBE, { event_type: eventType });
    }
    async pollEvents(maxEvents = 50) {
        const resp = await this._send(SyscallOp.SYS_POLL_EVENTS, {
            max_events: maxEvents,
        });
        return resp.events ?? [];
    }
    async emit(eventType, data) {
        const payload = { event_type: eventType };
        if (data !== undefined)
            payload.data = data;
        return this._send(SyscallOp.SYS_EMIT, payload);
    }
    // ── State ─────────────────────────────────────────────────────
    async store(key, value, scope = "global", ttlMs = 0) {
        return this._send(SyscallOp.SYS_STORE, {
            key,
            value,
            scope,
            ttl_ms: ttlMs,
        });
    }
    async fetch(key) {
        const resp = await this._send(SyscallOp.SYS_FETCH, { key });
        return resp.value;
    }
    async delete(key) {
        return this._send(SyscallOp.SYS_DELETE, { key });
    }
    async keys(prefix = "") {
        const resp = await this._send(SyscallOp.SYS_KEYS, { prefix });
        return resp.keys ?? [];
    }
    // ── Permissions ───────────────────────────────────────────────
    async getPermissions(agentId) {
        const payload = {};
        if (agentId !== undefined)
            payload.agent_id = agentId;
        return this._send(SyscallOp.SYS_GET_PERMS, Object.keys(payload).length > 0 ? payload : null);
    }
    async setPermissions(permissions, agentId) {
        const payload = { permissions };
        if (agentId !== undefined)
            payload.agent_id = agentId;
        return this._send(SyscallOp.SYS_SET_PERMS, payload);
    }
    // ── HTTP ──────────────────────────────────────────────────────
    async http(url, method = "GET", body, headers) {
        const payload = { url, method };
        if (body !== undefined)
            payload.body = body;
        if (headers !== undefined)
            payload.headers = headers;
        return this._send(SyscallOp.SYS_HTTP, payload);
    }
    // ── File I/O ──────────────────────────────────────────────────
    async exec(command, args) {
        const payload = { command };
        if (args !== undefined)
            payload.args = args;
        return this._send(SyscallOp.SYS_EXEC, payload);
    }
    async read(path) {
        return this._send(SyscallOp.SYS_READ, { path });
    }
    async write(path, content) {
        return this._send(SyscallOp.SYS_WRITE, { path, content });
    }
    // ── Memory Blocks ─────────────────────────────────────────────
    async memCreate(name, type = "core", access = "private", content = "", maxTokens = 0) {
        const payload = { name, type, access, content };
        if (maxTokens > 0)
            payload.max_tokens = maxTokens;
        return this._send(SyscallOp.SYS_MEM_CREATE, payload);
    }
    async memRead(id, name) {
        const payload = {};
        if (id)
            payload.id = id;
        if (name)
            payload.name = name;
        return this._send(SyscallOp.SYS_MEM_READ, payload);
    }
    async memWrite(id, content) {
        return this._send(SyscallOp.SYS_MEM_WRITE, { id, content });
    }
    async memAppend(id, content) {
        return this._send(SyscallOp.SYS_MEM_APPEND, { id, content });
    }
    async memDelete(id) {
        return this._send(SyscallOp.SYS_MEM_DELETE, { id });
    }
    async memList(limit = 100) {
        const resp = await this._send(SyscallOp.SYS_MEM_LIST, { limit });
        return resp.blocks ?? [];
    }
    async memShare(id, targetAgentId) {
        return this._send(SyscallOp.SYS_MEM_SHARE, {
            id,
            target_agent_id: targetAgentId,
        });
    }
    // ── Context / Artifacts ───────────────────────────────────────
    async docCreate(type, title, content, chainId, parentIds, metadata) {
        const payload = {
            type,
            title,
            content,
            chain_id: chainId,
        };
        if (parentIds)
            payload.parent_ids = parentIds;
        if (metadata)
            payload.metadata = metadata;
        return this._send(SyscallOp.SYS_DOC_CREATE, payload);
    }
    async docRead(id) {
        return this._send(SyscallOp.SYS_DOC_READ, { id });
    }
    async docUpdate(id, content, state) {
        const payload = { id };
        if (content !== undefined)
            payload.content = content;
        if (state !== undefined)
            payload.state = state;
        return this._send(SyscallOp.SYS_DOC_UPDATE, payload);
    }
    async docList(chainId, type, state, limit = 100) {
        const payload = { limit };
        if (chainId)
            payload.chain_id = chainId;
        if (type)
            payload.type = type;
        if (state)
            payload.state = state;
        const resp = await this._send(SyscallOp.SYS_DOC_LIST, payload);
        return resp.artifacts ?? [];
    }
    async docDelete(id) {
        return this._send(SyscallOp.SYS_DOC_DELETE, { id });
    }
    async chainCreate(name, description = "", metadata) {
        const payload = { name, description };
        if (metadata)
            payload.metadata = metadata;
        return this._send(SyscallOp.SYS_CHAIN_CREATE, payload);
    }
    async chainGet(id) {
        return this._send(SyscallOp.SYS_CHAIN_GET, { id });
    }
    async chainFork(chainId, atArtifactId) {
        return this._send(SyscallOp.SYS_CHAIN_FORK, {
            chain_id: chainId,
            at_artifact_id: atArtifactId,
        });
    }
    async contextAssemble(chainId, maxTokens = 128_000) {
        return this._send(SyscallOp.SYS_CONTEXT_ASSEMBLE, {
            chain_id: chainId,
            max_tokens: maxTokens,
        });
    }
    // ── Budget ────────────────────────────────────────────────────
    async setBudget(options = {}) {
        const payload = {};
        if (options.agentId !== undefined)
            payload.agent_id = options.agentId;
        if (options.maxTokens !== undefined)
            payload.max_tokens = options.maxTokens;
        if (options.maxSteps !== undefined)
            payload.max_steps = options.maxSteps;
        if (options.maxTimeMs !== undefined)
            payload.max_time_ms = options.maxTimeMs;
        if (options.maxCostUsd !== undefined)
            payload.max_cost_usd = options.maxCostUsd;
        if (options.killOnExceeded)
            payload.kill_on_exceeded = true;
        if (options.reset)
            payload.reset = true;
        return this._send(SyscallOp.SYS_SET_BUDGET, payload);
    }
    async getBudget(agentId) {
        const payload = {};
        if (agentId !== undefined)
            payload.agent_id = agentId;
        return this._send(SyscallOp.SYS_GET_BUDGET, Object.keys(payload).length > 0 ? payload : null);
    }
    // ── Priority / Scheduling ────────────────────────────────────
    async setPriority(priority = "normal", agentId) {
        const payload = { priority };
        if (agentId !== undefined)
            payload.agent_id = agentId;
        return this._send(SyscallOp.SYS_SET_PRIORITY, payload);
    }
    async getPriority(agentId) {
        const payload = {};
        if (agentId !== undefined)
            payload.agent_id = agentId;
        return this._send(SyscallOp.SYS_GET_PRIORITY, Object.keys(payload).length > 0 ? payload : null);
    }
    // ── MCP ───────────────────────────────────────────────────────
    async mcpCall(server, tool, args) {
        const payload = { server, tool };
        if (args !== undefined)
            payload.arguments = args;
        return this._send(SyscallOp.SYS_MCP_CALL, payload);
    }
    async mcpList() {
        return this._send(SyscallOp.SYS_MCP_LIST);
    }
    // ── PII ───────────────────────────────────────────────────────
    async piiScan(text) {
        return this._send(SyscallOp.SYS_PII_SCAN, { text });
    }
    async piiRedact(text) {
        return this._send(SyscallOp.SYS_PII_REDACT, { text });
    }
    // ── Metrics ───────────────────────────────────────────────────
    async metricsSystem() {
        return this._send(SyscallOp.SYS_METRICS_SYSTEM);
    }
    async metricsAgent(agentId) {
        const payload = {};
        if (agentId !== undefined)
            payload.agent_id = agentId;
        return this._send(SyscallOp.SYS_METRICS_AGENT, Object.keys(payload).length > 0 ? payload : null);
    }
    async metricsAllAgents() {
        return this._send(SyscallOp.SYS_METRICS_ALL_AGENTS);
    }
    async metricsCgroup() {
        return this._send(SyscallOp.SYS_METRICS_CGROUP);
    }
    // ── Worlds ────────────────────────────────────────────────────
    async worldCreate(name, metadata) {
        const payload = { name };
        if (metadata !== undefined)
            payload.metadata = metadata;
        return this._send(SyscallOp.SYS_WORLD_CREATE, payload);
    }
    async worldDestroy(worldId) {
        return this._send(SyscallOp.SYS_WORLD_DESTROY, { world_id: worldId });
    }
    async worldList() {
        const resp = await this._send(SyscallOp.SYS_WORLD_LIST);
        return resp.worlds ?? [];
    }
    async worldJoin(worldId) {
        return this._send(SyscallOp.SYS_WORLD_JOIN, { world_id: worldId });
    }
    async worldLeave(worldId) {
        return this._send(SyscallOp.SYS_WORLD_LEAVE, { world_id: worldId });
    }
    async worldEvent(worldId, eventType, data) {
        const payload = {
            world_id: worldId,
            event_type: eventType,
        };
        if (data !== undefined)
            payload.data = data;
        return this._send(SyscallOp.SYS_WORLD_EVENT, payload);
    }
    async worldState(worldId) {
        return this._send(SyscallOp.SYS_WORLD_STATE, { world_id: worldId });
    }
    async worldSnapshot(worldId) {
        return this._send(SyscallOp.SYS_WORLD_SNAPSHOT, { world_id: worldId });
    }
    async worldRestore(worldId, snapshotId) {
        return this._send(SyscallOp.SYS_WORLD_RESTORE, {
            world_id: worldId,
            snapshot_id: snapshotId,
        });
    }
    // ── Tunnels ───────────────────────────────────────────────────
    async tunnelConnect(remoteUrl, config) {
        const payload = { remote_url: remoteUrl };
        if (config !== undefined)
            payload.config = config;
        return this._send(SyscallOp.SYS_TUNNEL_CONNECT, payload);
    }
    async tunnelDisconnect(tunnelId) {
        return this._send(SyscallOp.SYS_TUNNEL_DISCONNECT, {
            tunnel_id: tunnelId,
        });
    }
    async tunnelStatus(tunnelId) {
        return this._send(SyscallOp.SYS_TUNNEL_STATUS, { tunnel_id: tunnelId });
    }
    async tunnelListRemotes() {
        return this._send(SyscallOp.SYS_TUNNEL_LIST_REMOTES);
    }
    async tunnelConfig(tunnelId, config) {
        return this._send(SyscallOp.SYS_TUNNEL_CONFIG, {
            tunnel_id: tunnelId,
            config,
        });
    }
    // ── Recording / Replay ────────────────────────────────────────
    async recordStart(maxEntries = 50_000) {
        return this._send(SyscallOp.SYS_RECORD_START, {
            max_entries: maxEntries,
        });
    }
    async recordStop() {
        return this._send(SyscallOp.SYS_RECORD_STOP);
    }
    async recordStatus() {
        return this._send(SyscallOp.SYS_RECORD_STATUS);
    }
    async replayStart() {
        return this._send(SyscallOp.SYS_REPLAY_START);
    }
    async replayStatus() {
        return this._send(SyscallOp.SYS_REPLAY_STATUS);
    }
    // ── Audit ─────────────────────────────────────────────────────
    async getAuditLog(category, limit = 100) {
        const payload = { limit };
        if (category !== undefined)
            payload.category = category;
        return this._send(SyscallOp.SYS_GET_AUDIT_LOG, payload);
    }
    async setAuditConfig(config) {
        return this._send(SyscallOp.SYS_SET_AUDIT_CONFIG, config);
    }
    // ── A2A ───────────────────────────────────────────────────────
    async a2aSend(targetUrl, message) {
        return this._send(SyscallOp.SYS_A2A_SEND, {
            target_url: targetUrl,
            message,
        });
    }
    async a2aRecv(maxCount = 50) {
        return this._send(SyscallOp.SYS_A2A_RECV, { max_count: maxCount });
    }
    // ── Credentials ───────────────────────────────────────────────
    async credsGet(name) {
        return this._send(SyscallOp.SYS_CREDS_GET, { name });
    }
    // ── OTel ──────────────────────────────────────────────────────
    async otelSpan(name, attributes) {
        const payload = { name };
        if (attributes !== undefined)
            payload.attributes = attributes;
        return this._send(SyscallOp.SYS_OTEL_SPAN, payload);
    }
    // ── LLM ───────────────────────────────────────────────────────
    async llmConfig() {
        return this._send(SyscallOp.SYS_LLM_CONFIG);
    }
    async llmReport() {
        return this._send(SyscallOp.SYS_LLM_REPORT);
    }
    // ── Async ─────────────────────────────────────────────────────
    async asyncPoll(taskId) {
        return this._send(SyscallOp.SYS_ASYNC_POLL, { task_id: taskId });
    }
}
//# sourceMappingURL=client.js.map