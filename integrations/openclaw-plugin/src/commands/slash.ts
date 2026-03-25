/**
 * /clove — Slash command handler for chat interface.
 *
 * Users type /clove in WhatsApp/Telegram/Slack to manage the kernel.
 */

import type { PluginCommandContext, PluginCommandResult, OpenClawPluginApi } from '../index.js'
import { getPluginConfig } from '../index.js'
import { isKernelRunning, getKernelCost, getKernelMetrics } from '../kernel-manager.js'

export async function handleSlashCommand(
  ctx: PluginCommandContext,
  api: OpenClawPluginApi,
): Promise<PluginCommandResult> {
  const cfg = getPluginConfig(api)
  const args = (ctx.args || '').trim().toLowerCase()

  // /clove status
  if (!args || args === 'status') {
    const running = await isKernelRunning(cfg)
    if (!running) {
      return { text: '🔴 CLOVE kernel not running.\nRun `openclaw clove launch` to start.' }
    }

    const cost = await getKernelCost(cfg)
    const metrics = await getKernelMetrics(cfg)
    const totalCost = (cost?.['total_cost_usd'] as number ?? 0).toFixed(4)
    const maxCost = cost?.['max_cost_usd'] as number ?? 0
    const agentCount = metrics?.['agent_count'] as number ?? 0
    const auditEntries = metrics?.['audit_entries'] as number ?? 0

    let text = '🟢 CLOVE Kernel Running\n'
    text += `\nAgents: ${agentCount} active`
    text += `\nCost: $${totalCost}${maxCost > 0 ? ` / $${maxCost.toFixed(2)}` : ''}`
    text += `\nAudit: ${auditEntries} entries`
    text += `\nDashboard: http://localhost:${cfg.apiPort}/dashboard`
    return { text }
  }

  // /clove budget
  if (args === 'budget' || args === 'cost') {
    const cost = await getKernelCost(cfg)
    if (!cost) return { text: '🔴 Kernel not running' }

    const totalCost = (cost['total_cost_usd'] as number ?? 0).toFixed(4)
    const maxCost = cost['max_cost_usd'] as number ?? 0
    const requests = cost['total_requests'] as number ?? 0
    const completed = cost['total_completed'] as number ?? 0

    let text = '💰 CLOVE Cost Report\n'
    text += `\nTotal spend: $${totalCost}`
    if (maxCost > 0) text += ` / $${maxCost.toFixed(2)}`
    text += `\nRequests: ${completed} / ${requests}`
    return { text }
  }

  // /clove help
  if (args === 'help') {
    return {
      text: '🔧 CLOVE Commands\n\n'
        + '/clove status — Kernel health + cost\n'
        + '/clove budget — Cost breakdown\n'
        + '/clove help — This message\n'
        + '\nCLI: openclaw clove {launch,status,stop,fleet}',
    }
  }

  return { text: `Unknown command: /clove ${args}\nTry: /clove help` }
}
