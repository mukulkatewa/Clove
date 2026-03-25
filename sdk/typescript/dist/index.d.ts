/**
 * @clove/sdk — TypeScript SDK for the CLOVE kernel.
 *
 * Communicates over a Unix domain socket using the binary IPC protocol
 * (17-byte little-endian header + UTF-8 JSON payload).
 */
export { PROTOCOL_MAGIC, HEADER_SIZE, SyscallOp, packMessage, unpackHeader, type HeaderFields, } from "./protocol.js";
export { CloveClient } from "./client.js";
export type { KernelResponse, HelloResponse, ThinkResponse, SpawnResponse, AgentInfo, BudgetResponse, PriorityResponse, MemoryBlock, Artifact, Chain, ContextAssemblyResponse, HttpResponse, ExecResponse, ReadFileResponse, Message, Event, World, PiiScanResponse, PiiRedactResponse, McpListResponse, SystemMetricsResponse, AgentMetricsResponse, LlmConfigResponse, LlmReportResponse, RecordingStatusResponse, AuditLogResponse, TunnelResponse, OtelSpanResponse, CredsResponse, Permissions, } from "./types.js";
//# sourceMappingURL=index.d.ts.map