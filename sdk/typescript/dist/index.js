/**
 * @clove/sdk — TypeScript SDK for the CLOVE kernel.
 *
 * Communicates over a Unix domain socket using the binary IPC protocol
 * (17-byte little-endian header + UTF-8 JSON payload).
 */
export { PROTOCOL_MAGIC, HEADER_SIZE, SyscallOp, packMessage, unpackHeader, } from "./protocol.js";
export { CloveClient } from "./client.js";
//# sourceMappingURL=index.js.map