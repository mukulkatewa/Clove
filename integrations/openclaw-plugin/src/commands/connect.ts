/**
 * openclaw clove connect — Open interactive shell into the CLOVE sandbox.
 */

import { execSync } from 'node:child_process'
import type { PluginLogger, ClovePluginConfig } from '../index.js'
import { isKernelRunning } from '../kernel-manager.js'

export interface ConnectOptions {
  logger: PluginLogger
  pluginConfig: ClovePluginConfig
}

export async function cliConnect(opts: ConnectOptions): Promise<void> {
  const { logger, pluginConfig: cfg } = opts

  if (!await isKernelRunning(cfg)) {
    logger.error('CLOVE kernel not running. Start with: openclaw clove launch')
    return
  }

  logger.info('Connecting to CLOVE sandbox...')
  logger.info('Note: All commands are audited and permission-gated by the kernel.')
  logger.info('')

  // Use the CLI tool if available, otherwise direct API
  try {
    execSync('clove_cli agents', { stdio: 'inherit' })
  } catch {
    // Fallback: show status via API
    const resp = await fetch(`http://localhost:${cfg.apiPort}/api/agents`)
    if (resp.ok) {
      const data = await resp.json() as Array<Record<string, unknown>>
      logger.info(`Active agents: ${data.length}`)
      for (const a of data) {
        logger.info(`  #${a['id']} ${a['name']} (${a['state']})`)
      }
    }
  }
}
