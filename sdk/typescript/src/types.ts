/**
 * TypeScript interfaces for CLOVE kernel response types.
 */

/** Generic kernel response with status. */
export interface KernelResponse {
  status?: string;
  error?: string;
  [key: string]: unknown;
}

/** Response from SYS_HELLO. */
export interface HelloResponse {
  version?: string;
  agent_id?: number;
  capabilities?: string[];
  [key: string]: unknown;
}

/** Response from SYS_THINK. */
export interface ThinkResponse {
  response?: string;
  model?: string;
  tokens_used?: number;
  cost_usd?: number;
  [key: string]: unknown;
}

/** Response from SYS_SPAWN. */
export interface SpawnResponse {
  agent_id?: number;
  name?: string;
  status?: string;
  [key: string]: unknown;
}

/** Response from SYS_LIST — individual agent entry. */
export interface AgentInfo {
  agent_id?: number;
  name?: string;
  status?: string;
  command?: string;
  [key: string]: unknown;
}

/** Response from SYS_GET_BUDGET / SYS_SET_BUDGET. */
export interface BudgetResponse {
  agent_id?: number;
  max_tokens?: number;
  max_steps?: number;
  max_time_ms?: number;
  max_cost_usd?: number;
  used_tokens?: number;
  used_steps?: number;
  used_time_ms?: number;
  used_cost_usd?: number;
  kill_on_exceeded?: boolean;
  [key: string]: unknown;
}

/** Response from SYS_GET_PRIORITY / SYS_SET_PRIORITY. */
export interface PriorityResponse {
  agent_id?: number;
  priority?: string;
  [key: string]: unknown;
}

/** A memory block entry. */
export interface MemoryBlock {
  id?: string;
  name?: string;
  type?: string;
  access?: string;
  content?: string;
  owner_agent_id?: number;
  max_tokens?: number;
  token_count?: number;
  created_at?: string;
  updated_at?: string;
  [key: string]: unknown;
}

/** An artifact (document) in the context layer. */
export interface Artifact {
  id?: string;
  type?: string;
  title?: string;
  content?: string;
  chain_id?: string;
  parent_ids?: string[];
  state?: string;
  created_by?: number;
  created_at?: string;
  updated_at?: string;
  metadata?: Record<string, unknown>;
  [key: string]: unknown;
}

/** A chain in the context layer. */
export interface Chain {
  id?: string;
  name?: string;
  description?: string;
  artifact_count?: number;
  created_at?: string;
  metadata?: Record<string, unknown>;
  [key: string]: unknown;
}

/** Response from SYS_CONTEXT_ASSEMBLE. */
export interface ContextAssemblyResponse {
  chain_id?: string;
  assembled_text?: string;
  token_count?: number;
  artifact_count?: number;
  [key: string]: unknown;
}

/** Response from SYS_HTTP. */
export interface HttpResponse {
  status_code?: number;
  body?: string;
  headers?: Record<string, string>;
  [key: string]: unknown;
}

/** Response from SYS_EXEC. */
export interface ExecResponse {
  stdout?: string;
  stderr?: string;
  exit_code?: number;
  [key: string]: unknown;
}

/** Response from SYS_READ. */
export interface ReadFileResponse {
  content?: string;
  path?: string;
  [key: string]: unknown;
}

/** An IPC message. */
export interface Message {
  sender_id?: number;
  content?: string;
  timestamp?: string;
  [key: string]: unknown;
}

/** An event from the event bus. */
export interface Event {
  event_type?: string;
  data?: Record<string, unknown>;
  timestamp?: string;
  [key: string]: unknown;
}

/** A world entry. */
export interface World {
  world_id?: number;
  name?: string;
  metadata?: Record<string, unknown>;
  [key: string]: unknown;
}

/** Response from SYS_PII_SCAN. */
export interface PiiScanResponse {
  entities?: Array<{
    type?: string;
    text?: string;
    start?: number;
    end?: number;
  }>;
  [key: string]: unknown;
}

/** Response from SYS_PII_REDACT. */
export interface PiiRedactResponse {
  redacted_text?: string;
  [key: string]: unknown;
}

/** Response from SYS_MCP_LIST. */
export interface McpListResponse {
  servers?: Array<{
    name?: string;
    tools?: string[];
  }>;
  [key: string]: unknown;
}

/** Response from SYS_METRICS_SYSTEM. */
export interface SystemMetricsResponse {
  cpu_percent?: number;
  memory_mb?: number;
  agents_active?: number;
  uptime_seconds?: number;
  [key: string]: unknown;
}

/** Response from SYS_METRICS_AGENT. */
export interface AgentMetricsResponse {
  agent_id?: number;
  tokens_used?: number;
  steps_taken?: number;
  cost_usd?: number;
  [key: string]: unknown;
}

/** Response from SYS_LLM_CONFIG. */
export interface LlmConfigResponse {
  provider?: string;
  model?: string;
  max_tokens?: number;
  [key: string]: unknown;
}

/** Response from SYS_LLM_REPORT. */
export interface LlmReportResponse {
  total_requests?: number;
  total_tokens?: number;
  total_cost_usd?: number;
  [key: string]: unknown;
}

/** Response from recording/replay status calls. */
export interface RecordingStatusResponse {
  recording?: boolean;
  entry_count?: number;
  [key: string]: unknown;
}

/** Response from SYS_GET_AUDIT_LOG. */
export interface AuditLogResponse {
  entries?: Array<Record<string, unknown>>;
  [key: string]: unknown;
}

/** Response from tunnel operations. */
export interface TunnelResponse {
  status?: string;
  tunnel_id?: string;
  [key: string]: unknown;
}

/** Response from SYS_OTEL_SPAN. */
export interface OtelSpanResponse {
  span_id?: string;
  trace_id?: string;
  [key: string]: unknown;
}

/** Response from SYS_CREDS_GET. */
export interface CredsResponse {
  value?: string;
  [key: string]: unknown;
}

/** Permissions map. */
export interface Permissions {
  [key: string]: boolean | string | number;
}
