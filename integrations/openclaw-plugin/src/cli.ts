/**
 * CLI registrar for `openclaw clove <subcommand>`.
 */

import type { OpenClawPluginApi, PluginCliContext } from './index.js'
import { getPluginConfig } from './index.js'
import { cliLaunch } from './commands/launch.js'
import { cliStatus } from './commands/status.js'
import { cliStop } from './commands/stop.js'
import { cliFleet } from './commands/fleet.js'
import { cliConnect } from './commands/connect.js'

export function registerCliCommands(ctx: PluginCliContext, api: OpenClawPluginApi): void {
  const { program, logger } = ctx
  const pluginConfig = getPluginConfig(api)

  const clove = program.command('clove').description('CLOVE kernel sandbox management')

  // openclaw clove launch
  clove
    .command('launch')
    .description('Start CLOVE kernel and sandbox OpenClaw inside it')
    .option('--budget <usd>', 'LLM budget in USD', String(pluginConfig.budgetMaxCostUsd))
    .option('--privacy <mode>', 'PII mode: audit, redact, block', pluginConfig.privacyMode)
    .action(async (opts: { budget: string; privacy: string }) => {
      await cliLaunch({
        budget: parseFloat(opts.budget),
        privacyMode: opts.privacy,
        logger,
        pluginConfig,
      })
    })

  // openclaw clove status
  clove
    .command('status')
    .description('Show kernel health, agents, cost, budget')
    .option('--json', 'Output as JSON', false)
    .action(async (opts: { json: boolean }) => {
      await cliStatus({ json: opts.json, logger, pluginConfig })
    })

  // openclaw clove stop
  clove
    .command('stop')
    .description('Stop CLOVE kernel and all agents')
    .action(async () => {
      await cliStop({ logger, pluginConfig })
    })

  // openclaw clove fleet
  clove
    .command('fleet')
    .description('Launch multiple OpenClaw agents with per-agent budgets and permissions')
    .option('--config <path>', 'Fleet YAML config file', 'clove-fleet.yaml')
    .option('--goal <text>', 'Goal for all agents (overrides config)')
    .option('--agents <count>', 'Number of agents', '3')
    .option('--budget <usd>', 'Total budget', '5.0')
    .action(async (opts: { config: string; goal?: string; agents: string; budget: string }) => {
      await cliFleet({
        configPath: opts.config,
        goal: opts.goal,
        agentCount: parseInt(opts.agents, 10),
        totalBudget: parseFloat(opts.budget),
        logger,
        pluginConfig,
      })
    })

  // openclaw clove connect
  clove
    .command('connect')
    .description('Open shell into the CLOVE sandbox')
    .action(async () => {
      await cliConnect({ logger, pluginConfig })
    })
}
