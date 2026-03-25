/**
 * Synchronous-style client for the CLOVE kernel over Unix domain sockets.
 *
 * Uses Node.js `net` module with promise-based wrappers around the
 * stream-oriented socket for a clean async/await API.
 */
import type { AgentInfo, AgentMetricsResponse, Artifact, AuditLogResponse, BudgetResponse, Chain, ContextAssemblyResponse, CredsResponse, ExecResponse, HelloResponse, HttpResponse, KernelResponse, LlmConfigResponse, LlmReportResponse, McpListResponse, MemoryBlock, Message, Event, OtelSpanResponse, Permissions, PiiRedactResponse, PiiScanResponse, PriorityResponse, ReadFileResponse, RecordingStatusResponse, SpawnResponse, SystemMetricsResponse, ThinkResponse, TunnelResponse, World } from "./types.js";
/**
 * Low-level CLOVE kernel client.
 *
 * Communicates over a Unix domain socket using the binary IPC protocol
 * (17-byte header + JSON payload). All public methods return Promises.
 */
export declare class CloveClient {
    readonly socketPath: string;
    readonly agentId: number;
    private socket;
    private buffer;
    private pendingResolve;
    private pendingBytes;
    constructor(socketPath?: string, agentId?: number);
    /**
     * Open the Unix socket connection.
     * @returns Resolves to `true` on successful connection.
     */
    connect(): Promise<boolean>;
    /** Close the socket if open. */
    disconnect(): void;
    private tryFlush;
    /**
     * Read exactly `n` bytes from the socket.
     */
    private _recvExact;
    /**
     * Send a syscall and wait for the kernel's response.
     */
    private _send;
    hello(): Promise<HelloResponse>;
    think(prompt: string): Promise<ThinkResponse>;
    thinkWithContext(prompt: string, chainId: string, model?: string): Promise<ThinkResponse>;
    spawn(name: string, command: string): Promise<SpawnResponse>;
    kill(agentId: number): Promise<KernelResponse>;
    listAgents(): Promise<AgentInfo[]>;
    pause(agentId: number): Promise<KernelResponse>;
    resume(agentId: number): Promise<KernelResponse>;
    sendMessage(targetId: number, content: string): Promise<KernelResponse>;
    recvMessages(maxCount?: number): Promise<Message[]>;
    registerName(name: string): Promise<KernelResponse>;
    subscribe(eventType: string): Promise<KernelResponse>;
    unsubscribe(eventType: string): Promise<KernelResponse>;
    pollEvents(maxEvents?: number): Promise<Event[]>;
    emit(eventType: string, data?: Record<string, unknown>): Promise<KernelResponse>;
    store(key: string, value: unknown, scope?: string, ttlMs?: number): Promise<KernelResponse>;
    fetch(key: string): Promise<unknown>;
    delete(key: string): Promise<KernelResponse>;
    keys(prefix?: string): Promise<string[]>;
    getPermissions(agentId?: number): Promise<KernelResponse>;
    setPermissions(permissions: Permissions, agentId?: number): Promise<KernelResponse>;
    http(url: string, method?: string, body?: string, headers?: Record<string, string>): Promise<HttpResponse>;
    exec(command: string, args?: string[]): Promise<ExecResponse>;
    read(path: string): Promise<ReadFileResponse>;
    write(path: string, content: string): Promise<KernelResponse>;
    memCreate(name: string, type?: string, access?: string, content?: string, maxTokens?: number): Promise<MemoryBlock>;
    memRead(id?: string, name?: string): Promise<MemoryBlock>;
    memWrite(id: string, content: string): Promise<KernelResponse>;
    memAppend(id: string, content: string): Promise<KernelResponse>;
    memDelete(id: string): Promise<KernelResponse>;
    memList(limit?: number): Promise<MemoryBlock[]>;
    memShare(id: string, targetAgentId: number): Promise<KernelResponse>;
    docCreate(type: string, title: string, content: string, chainId: string, parentIds?: string[], metadata?: Record<string, unknown>): Promise<Artifact>;
    docRead(id: string): Promise<Artifact>;
    docUpdate(id: string, content?: string, state?: string): Promise<KernelResponse>;
    docList(chainId?: string, type?: string, state?: string, limit?: number): Promise<Artifact[]>;
    docDelete(id: string): Promise<KernelResponse>;
    chainCreate(name: string, description?: string, metadata?: Record<string, unknown>): Promise<Chain>;
    chainGet(id: string): Promise<Chain>;
    chainFork(chainId: string, atArtifactId: string): Promise<Chain>;
    contextAssemble(chainId: string, maxTokens?: number): Promise<ContextAssemblyResponse>;
    setBudget(options?: {
        agentId?: number;
        maxTokens?: number;
        maxSteps?: number;
        maxTimeMs?: number;
        maxCostUsd?: number;
        killOnExceeded?: boolean;
        reset?: boolean;
    }): Promise<BudgetResponse>;
    getBudget(agentId?: number): Promise<BudgetResponse>;
    setPriority(priority?: string, agentId?: number): Promise<PriorityResponse>;
    getPriority(agentId?: number): Promise<PriorityResponse>;
    mcpCall(server: string, tool: string, args?: Record<string, unknown>): Promise<KernelResponse>;
    mcpList(): Promise<McpListResponse>;
    piiScan(text: string): Promise<PiiScanResponse>;
    piiRedact(text: string): Promise<PiiRedactResponse>;
    metricsSystem(): Promise<SystemMetricsResponse>;
    metricsAgent(agentId?: number): Promise<AgentMetricsResponse>;
    metricsAllAgents(): Promise<KernelResponse>;
    metricsCgroup(): Promise<KernelResponse>;
    worldCreate(name: string, metadata?: Record<string, unknown>): Promise<KernelResponse>;
    worldDestroy(worldId: number): Promise<KernelResponse>;
    worldList(): Promise<World[]>;
    worldJoin(worldId: number): Promise<KernelResponse>;
    worldLeave(worldId: number): Promise<KernelResponse>;
    worldEvent(worldId: number, eventType: string, data?: Record<string, unknown>): Promise<KernelResponse>;
    worldState(worldId: number): Promise<KernelResponse>;
    worldSnapshot(worldId: number): Promise<KernelResponse>;
    worldRestore(worldId: number, snapshotId: string): Promise<KernelResponse>;
    tunnelConnect(remoteUrl: string, config?: Record<string, unknown>): Promise<TunnelResponse>;
    tunnelDisconnect(tunnelId: string): Promise<KernelResponse>;
    tunnelStatus(tunnelId: string): Promise<TunnelResponse>;
    tunnelListRemotes(): Promise<KernelResponse>;
    tunnelConfig(tunnelId: string, config: Record<string, unknown>): Promise<KernelResponse>;
    recordStart(maxEntries?: number): Promise<KernelResponse>;
    recordStop(): Promise<KernelResponse>;
    recordStatus(): Promise<RecordingStatusResponse>;
    replayStart(): Promise<KernelResponse>;
    replayStatus(): Promise<RecordingStatusResponse>;
    getAuditLog(category?: string, limit?: number): Promise<AuditLogResponse>;
    setAuditConfig(config: Record<string, unknown>): Promise<KernelResponse>;
    a2aSend(targetUrl: string, message: Record<string, unknown>): Promise<KernelResponse>;
    a2aRecv(maxCount?: number): Promise<KernelResponse>;
    credsGet(name: string): Promise<CredsResponse>;
    otelSpan(name: string, attributes?: Record<string, unknown>): Promise<OtelSpanResponse>;
    llmConfig(): Promise<LlmConfigResponse>;
    llmReport(): Promise<LlmReportResponse>;
    asyncPoll(taskId: string): Promise<KernelResponse>;
}
//# sourceMappingURL=client.d.ts.map