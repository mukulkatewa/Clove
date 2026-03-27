# Bug: openclaw-mcp-bridge does not forward tool calls to stdio MCP servers

> Filed: 2026-03-27
> Affects: openclaw-mcp-bridge (installed via `openclaw plugins install openclaw-mcp-bridge`)
> OpenClaw version: 2026.3.11 (29dc654)
> Bridge version: latest as of 2026-03-27

## Summary

The `openclaw-mcp-bridge` plugin correctly discovers tools from a stdio-based MCP server, injects tool schemas into the agent's context, and the LLM correctly calls `<prefix>__call` with the right tool name and arguments. However, the bridge **never actually forwards the `tools/call` JSON-RPC request** to the stdio subprocess. The MCP server's stderr logs show zero incoming requests after the initial `initialize` + `tools/list` handshake.

## Reproduction

### 1. MCP Server (works perfectly standalone)

```typescript
// Minimal MCP server with one tool
import { Server } from '@modelcontextprotocol/sdk/server/index.js'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'

const server = new Server({ name: 'test', version: '0.1.0' }, { capabilities: { tools: {} } })

server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: [{ name: 'hello', description: 'Returns hello', inputSchema: { type: 'object', properties: {} } }]
}))

server.setRequestHandler(CallToolRequestSchema, async () => {
  console.error('[test-mcp] hello tool was called!')  // THIS NEVER APPEARS
  return { content: [{ type: 'text', text: 'Hello from MCP server!' }] }
})

const transport = new StdioServerTransport()
await server.connect(transport)
```

### 2. Standalone test (WORKS)

```bash
echo '{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"0.1"}}}
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"hello","arguments":{}}}' | node server.js
```

Output: `{"result":{"content":[{"type":"text","text":"Hello from MCP server!"}]},"jsonrpc":"2.0","id":1}`

**The MCP server works.**

### 3. OpenClaw config

```json
{
  "plugins": {
    "entries": {
      "openclaw-mcp-bridge": {
        "enabled": true,
        "config": {
          "servers": {
            "test": {
              "url": "stdio://local",
              "transport": "stdio",
              "command": "node",
              "args": ["/path/to/server.js"]
            }
          }
        }
      }
    }
  }
}
```

### 4. What happens

```
[gateway] mcp-client: registered proxy tool test__call for server "test"
[MCPManager] Connecting to server "test"...
[MCPManager] Connected to server "test": 1 tool(s) discovered.
```

Tools are discovered. Schema injected into agent context. LLM generates:

```
test__call(tool_name='hello', arguments={})
```

**But the MCP server's `CallToolRequestSchema` handler is never invoked.** No stderr logs. No response. The tool call returns empty or the agent gets no result.

## Root cause analysis

### Issue 1: Parameter name mismatch

The bridge registers the proxy tool with parameter `tool: Type.String()` (TypeBox schema). But the LLM generates `tool_name` instead of `tool`. In the bridge's `execute()` handler:

```javascript
const toolName = params.tool;  // undefined — LLM sent params.tool_name
```

**Fix applied (patched locally):**
```javascript
const toolName = params.tool || params.tool_name;
```

This fixes the parameter extraction but doesn't fix the actual forwarding issue.

### Issue 2: stdio subprocess lifecycle

The `MCPManager` connects to the stdio server during `ensureConnected()`, performs `initialize` + `tools/list`, and discovers tools. It appears the stdio subprocess connection is not maintained for subsequent `tools/call` requests, or the `callTool` method creates a new connection that doesn't complete the initialization handshake.

**Evidence:** After the initial discovery, no JSON-RPC messages are received by the MCP server (verified via stderr logging). The subprocess is alive (tested with `ps`) but receives no input on stdin.

### Issue 3: Tool namespacing

The bridge calls `mcpManager.callTool('${prefix}__${toolName}', args)`. For server "clove" with tool "recall", this becomes `clove__recall`. The MCPManager must map this back to server "clove", tool "recall" and forward to the correct subprocess. It's unclear if this mapping works correctly for stdio transports.

## Workaround

Write a native OpenClaw plugin that registers tools directly via `api.registerTool()` and calls the backend HTTP API instead of going through MCP:

```typescript
api.registerTool({
  name: 'clove_recall',
  async execute(_, params) {
    const res = await fetch('http://localhost:8080/api/memory')
    const data = await res.json()
    return { content: [{ type: 'text', text: JSON.stringify(data.blocks) }] }
  }
})
```

This bypasses the MCP bridge entirely and works reliably.

## Suggested fix for openclaw-mcp-bridge

1. Accept both `tool` and `tool_name` in proxy tool params
2. Verify stdio subprocess stdin pipe is kept open after initial handshake
3. Add debug logging for `callTool` invocations (currently fails silently)
4. Add a test: `register stdio server → discover tools → call tool → verify response`
