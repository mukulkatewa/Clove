/**
 * CLOVE — OpenClaw Plugin
 *
 * Runs OpenClaw inside the CLOVE kernel sandbox instead of Docker/OpenShell.
 * 122x faster startup. 324x less memory. Full audit trail.
 *
 * Mirrors NemoClaw's plugin pattern exactly:
 *   1. registerCommand()  → /clove slash command in chat
 *   2. registerCli()      → openclaw clove {launch,status,stop,fleet}
 *   3. registerProvider() → LLM calls route through kernel (cost tracked, PII filtered)
 *   4. registerService()  → kernel auto-starts/stops with gateway
 *
 * Types are stubs matching the OpenClaw Plugin SDK (only available at runtime).
 */

import type { Command } from 'commander'
import { registerCliCommands } from './cli.js'
import { handleSlashCommand } from './commands/slash.js'
import { startKernel, stopKernel, isKernelRunning } from './kernel-manager.js'

// ---------------------------------------------------------------------------
// OpenClaw Plugin SDK compatible types (mirrors openclaw/plugin-sdk)
// Copied from NemoClaw's pattern — SDK only available inside host process
// ---------------------------------------------------------------------------

export interface OpenClawConfig {
  [key: string]: unknown
}

export interface PluginLogger {
  info(message: string): void
  warn(message: string): void
  error(message: string): void
  debug(message: string): void
}

export interface PluginCommandContext {
  senderId?: string
  channel: string
  isAuthorizedSender: boolean
  args?: string
  commandBody: string
  config: OpenClawConfig
  from?: string
  to?: string
  accountId?: string
}

export interface PluginCommandResult {
  text?: string
  mediaUrl?: string
}

export interface PluginCommandDefinition {
  name: string
  description: string
  acceptsArgs?: boolean
  requireAuth?: boolean
  handler: (ctx: PluginCommandContext) => PluginCommandResult | Promise<PluginCommandResult>
}

export interface PluginCliContext {
  program: Command
  config: OpenClawConfig
  workspaceDir?: string
  logger: PluginLogger
}

export type PluginCliRegistrar = (ctx: PluginCliContext) => void | Promise<void>

export interface ProviderAuthMethod {
  type: string
  envVar?: string
  headerName?: string
  label?: string
}

export interface ModelProviderEntry {
  id: string
  label: string
  contextWindow?: number
  maxOutput?: number
}

export interface ModelProviderConfig {
  chat?: ModelProviderEntry[]
}

export interface ProviderPlugin {
  id: string
  label: string
  docsPath?: string
  aliases?: string[]
  envVars?: string[]
  models?: ModelProviderConfig
  auth: ProviderAuthMethod[]
}

export interface PluginService {
  id: string
  start: (ctx: { config: OpenClawConfig; logger: PluginLogger }) => void | Promise<void>
  stop?: (ctx: { config: OpenClawConfig; logger: PluginLogger }) => void | Promise<void>
}

export interface OpenClawPluginApi {
  id: string
  name: string
  version?: string
  config: OpenClawConfig
  pluginConfig?: Record<string, unknown>
  logger: PluginLogger
  registerCommand: (command: PluginCommandDefinition) => void
  registerCli: (registrar: PluginCliRegistrar, opts?: { commands?: string[] }) => void
  registerProvider: (provider: ProviderPlugin) => void
  registerService: (service: PluginService) => void
  resolvePath: (input: string) => string
  on: (hookName: string, handler: (...args: unknown[]) => void) => void
}

// ---------------------------------------------------------------------------
// Plugin config
// ---------------------------------------------------------------------------

export interface ClovePluginConfig {
  kernelPath: string
  socketPath: string
  apiPort: number
  budgetMaxCostUsd: number
  privacyEnabled: boolean
  privacyMode: string
  sandboxMemoryMb: number
  sandboxCpuPercent: number
  allowedDomains: string[]
}

const DEFAULTS: ClovePluginConfig = {
  kernelPath: '',
  socketPath: '/tmp/clove.sock',
  apiPort: 8080,
  budgetMaxCostUsd: 10.0,
  privacyEnabled: true,
  privacyMode: 'redact',
  sandboxMemoryMb: 512,
  sandboxCpuPercent: 50,
  allowedDomains: [
    'api.openai.com', 'api.anthropic.com', 'openrouter.ai',
    'generativelanguage.googleapis.com', 'api.groq.com',
    'api.telegram.org', 'discord.com', 'gateway.discord.gg',
    'slack.com', 'web.whatsapp.com', 'signal.org',
    'api.openclaw.ai', 'clawhub.dev', 'registry.npmjs.org',
  ],
}

export function getPluginConfig(api: OpenClawPluginApi): ClovePluginConfig {
  const raw = api.pluginConfig ?? {}
  return {
    kernelPath: typeof raw['kernelPath'] === 'string' ? raw['kernelPath'] : DEFAULTS.kernelPath,
    socketPath: typeof raw['socketPath'] === 'string' ? raw['socketPath'] : DEFAULTS.socketPath,
    apiPort: typeof raw['apiPort'] === 'number' ? raw['apiPort'] : DEFAULTS.apiPort,
    budgetMaxCostUsd: typeof raw['budgetMaxCostUsd'] === 'number' ? raw['budgetMaxCostUsd'] : DEFAULTS.budgetMaxCostUsd,
    privacyEnabled: typeof raw['privacyEnabled'] === 'boolean' ? raw['privacyEnabled'] : DEFAULTS.privacyEnabled,
    privacyMode: typeof raw['privacyMode'] === 'string' ? raw['privacyMode'] : DEFAULTS.privacyMode,
    sandboxMemoryMb: typeof raw['sandboxMemoryMb'] === 'number' ? raw['sandboxMemoryMb'] : DEFAULTS.sandboxMemoryMb,
    sandboxCpuPercent: typeof raw['sandboxCpuPercent'] === 'number' ? raw['sandboxCpuPercent'] : DEFAULTS.sandboxCpuPercent,
    allowedDomains: Array.isArray(raw['allowedDomains']) ? raw['allowedDomains'] as string[] : DEFAULTS.allowedDomains,
  }
}

// ---------------------------------------------------------------------------
// Plugin entry point
// ---------------------------------------------------------------------------

export default function register(api: OpenClawPluginApi): void {
  const cfg = getPluginConfig(api)

  // 1. /clove slash command (chat interface)
  api.registerCommand({
    name: 'clove',
    description: 'CLOVE kernel sandbox management (status, budget, fleet)',
    acceptsArgs: true,
    handler: (ctx) => handleSlashCommand(ctx, api),
  })

  // 2. `openclaw clove` CLI subcommands
  api.registerCli(
    (cliCtx) => registerCliCommands(cliCtx, api),
    { commands: ['clove'] },
  )

  // 3. CLOVE as inference provider — all LLM calls route through kernel
  api.registerProvider({
    id: 'clove',
    label: 'CLOVE Kernel (any provider, cost tracked, PII filtered)',
    aliases: ['clove-kernel', 'clove-sandbox'],
    envVars: ['OPENROUTER_API_KEY', 'OPENAI_API_KEY', 'ANTHROPIC_API_KEY', 'GROQ_API_KEY'],
    models: {
      chat: [
        { id: 'clove/auto', label: 'Auto (kernel routes to configured provider)', contextWindow: 200000, maxOutput: 16384 },
        { id: 'clove/anthropic/claude-sonnet-4', label: 'Claude Sonnet 4 (via kernel)', contextWindow: 200000, maxOutput: 16384 },
        { id: 'clove/openai/gpt-4o', label: 'GPT-4o (via kernel)', contextWindow: 128000, maxOutput: 16384 },
        { id: 'clove/google/gemini-2.0-flash', label: 'Gemini 2.0 Flash (via kernel)', contextWindow: 1000000, maxOutput: 8192 },
      ],
    },
    auth: [{
      type: 'bearer',
      envVar: 'OPENROUTER_API_KEY',
      headerName: 'Authorization',
      label: 'API Key (auto-detected from OPENROUTER/OPENAI/ANTHROPIC/GROQ env vars)',
    }],
  })

  // 4. CLOVE kernel as background service
  api.registerService({
    id: 'clove-kernel',
    start: async ({ logger }) => {
      if (await isKernelRunning(cfg)) {
        logger.info('CLOVE kernel already running')
        return
      }
      await startKernel(cfg, logger)
    },
    stop: async ({ logger }) => {
      await stopKernel(cfg, logger)
    },
  })

  // Banner
  api.logger.info('')
  api.logger.info('  ┌─────────────────────────────────────────────────────┐')
  api.logger.info('  │  CLOVE registered                                    │')
  api.logger.info('  │                                                       │')
  api.logger.info(`  │  Sandbox:    kernel (ns + seccomp + landlock)         │`)
  api.logger.info(`  │  Budget:     $${cfg.budgetMaxCostUsd.toFixed(2).padEnd(42)}│`)
  api.logger.info(`  │  Privacy:    ${cfg.privacyMode.padEnd(42)}│`)
  api.logger.info(`  │  API:        http://localhost:${cfg.apiPort}/api`.padEnd(56) + '│')
  api.logger.info(`  │  Dashboard:  http://localhost:${cfg.apiPort}/dashboard`.padEnd(56) + '│')
  api.logger.info('  │  Commands:   openclaw clove <command>                 │')
  api.logger.info('  └─────────────────────────────────────────────────────┘')
  api.logger.info('')
}
