/**
 * openclaw clove status — Show kernel health, agents, cost, budget.
 */

import type { PluginLogger, ClovePluginConfig } from '../index.js'
import { isKernelRunning, getKernelStatus, getKernelCost, getKernelMetrics } from '../kernel-manager.js'

export interface StatusOptions {
  json: boolean
  logger: PluginLogger
  pluginConfig: ClovePluginConfig
}

export async function cliStatus(opts: StatusOptions): Promise<void> {
  const { json: jsonOutput, logger, pluginConfig: cfg } = opts

  if (!await isKernelRunning(cfg)) {
    if (jsonOutput) {
      console.log(JSON.stringify({ status: 'stopped' }))
    } else {
      logger.info('CLOVE kernel: not running')
      logger.info('Run "openclaw clove launch" to start.')
    }
    return
  }

  const health = await getKernelStatus(cfg)
  const cost = await getKernelCost(cfg)
  const metrics = await getKernelMetrics(cfg)

  if (jsonOutput) {
    console.log(JSON.stringify({ status: 'running', health, cost, metrics }, null, 2))
    return
  }

  const uptime = health?.['uptime_s'] as number ?? 0
  const uptimeStr = uptime > 3600
    ? `${Math.floor(uptime / 3600)}h ${Math.floor((uptime % 3600) / 60)}m`
    : uptime > 60
    ? `${Math.floor(uptime / 60)}m ${uptime % 60}s`
    : `${uptime}s`

  const totalCost = (cost?.['total_cost_usd'] as number ?? 0).toFixed(4)
  const maxCost = cost?.['max_cost_usd'] as number ?? 0
  const budgetStr = maxCost > 0 ? `$${totalCost} / $${maxCost.toFixed(2)}` : `$${totalCost}`
  const agentCount = metrics?.['agent_count'] as number ?? 0
  const auditEntries = metrics?.['audit_entries'] as number ?? 0

  logger.info('')
  logger.info('  CLOVE Kernel Status')
  logger.info('  ────────────────────────────────────')
  logger.info(`  Status:     running`)
  logger.info(`  Uptime:     ${uptimeStr}`)
  logger.info(`  Agents:     ${agentCount} active`)
  logger.info(`  Cost:       ${budgetStr}`)
  logger.info(`  Audit:      ${auditEntries} entries`)
  logger.info(`  API:        http://localhost:${cfg.apiPort}`)
  logger.info(`  Dashboard:  http://localhost:${cfg.apiPort}/dashboard`)
  logger.info('  ────────────────────────────────────')
  logger.info('')
}
