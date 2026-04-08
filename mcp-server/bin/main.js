#!/usr/bin/env node
/**
 * CLOVE MCP — single entry point
 *
 * npx @cloveos/mcp-server           → start MCP server
 * npx @cloveos/mcp-server setup     → install skill + configure client
 * npx @cloveos/mcp-server setup --cursor / --windsurf / --vscode / --desktop / --prompt
 */

import { spawn } from "child_process";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const cmd = process.argv[2];

if (cmd === "setup") {
  // Pass remaining args through to setup.js
  import("./setup.js");
} else {
  // Start the MCP server
  const server = spawn("node", [join(__dirname, "../dist/index.js")], {
    stdio: "inherit",
    env: process.env,
  });
  server.on("exit", (code) => process.exit(code ?? 0));
}
