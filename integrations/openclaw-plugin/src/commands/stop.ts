/**
 * openclaw clove stop — Stop CLOVE kernel and all agents.
 */

import type { PluginLogger, ClovePluginConfig } from '../index.js'
import { stopKernel, isKernelRunning } from '../kernel-manager.js'

export interface StopOptions {
  logger: PluginLogger
  pluginConfig: ClovePluginConfig
}

export async function cliStop(opts: StopOptions): Promise<void> {
  const { logger, pluginConfig: cfg } = opts

  if (!await isKernelRunning(cfg)) {
    logger.info('CLOVE kernel is not running.')
    return
  }

  logger.info('Stopping CLOVE kernel...')
  await stopKernel(cfg, logger)
}
