/**
 * openclaw clove launch — Start CLOVE kernel and sandbox OpenClaw.
 */

import type { PluginLogger, ClovePluginConfig } from '../index.js'
import { startKernel, isKernelRunning, findKernelBinary } from '../kernel-manager.js'

export interface LaunchOptions {
  budget: number
  privacyMode: string
  logger: PluginLogger
  pluginConfig: ClovePluginConfig
}

export async function cliLaunch(opts: LaunchOptions): Promise<void> {
  const { budget, privacyMode, logger, pluginConfig } = opts

  // Override config with CLI args
  const cfg = { ...pluginConfig, budgetMaxCostUsd: budget, privacyMode }

  logger.info('CLOVE launch: setting up OpenClaw inside kernel sandbox')
  logger.info('')

  // Check if kernel binary exists
  const binary = findKernelBinary(cfg.kernelPath)
  if (!binary) {
    logger.error('CLOVE kernel binary not found.')
    logger.error('')
    logger.error('Install options:')
    logger.error('  brew install clove          # macOS')
    logger.error('  curl -fsSL cloveos.com/install | sh')
    logger.error('  # Or build from source: cmake --build build/')
    logger.error('')
    logger.error('Then retry: openclaw clove launch')
    return
  }

  logger.info(`Kernel binary: ${binary}`)

  // Check if already running
  if (await isKernelRunning(cfg)) {
    logger.info('CLOVE kernel already running.')
    logger.info('')
    logger.info('OpenClaw is already sandboxed. Use:')
    logger.info('  openclaw clove status    # check health')
    logger.info('  openclaw clove stop      # stop kernel')
    return
  }

  // Detect LLM provider
  const providers = [
    { key: 'OPENROUTER_API_KEY', name: 'OpenRouter (300+ models)' },
    { key: 'OPENAI_API_KEY', name: 'OpenAI' },
    { key: 'ANTHROPIC_API_KEY', name: 'Anthropic' },
    { key: 'GROQ_API_KEY', name: 'Groq' },
  ]
  let detectedProvider = 'none'
  for (const p of providers) {
    if (process.env[p.key]) {
      detectedProvider = p.name
      break
    }
  }

  logger.info(`LLM provider: ${detectedProvider}`)
  logger.info(`Budget: $${cfg.budgetMaxCostUsd.toFixed(2)}`)
  logger.info(`PII mode: ${cfg.privacyMode}`)
  logger.info('')

  if (detectedProvider === 'none') {
    logger.warn('No LLM API key detected. Set one of:')
    logger.warn('  export OPENROUTER_API_KEY=sk-or-...')
    logger.warn('  export OPENAI_API_KEY=sk-...')
    logger.warn('  export ANTHROPIC_API_KEY=sk-ant-...')
    logger.warn('')
  }

  // Start kernel
  logger.info('Starting CLOVE kernel...')
  const ok = await startKernel(cfg, logger)

  if (!ok) {
    logger.error('Failed to start kernel. Check logs:')
    logger.error('  cat /tmp/clove-kernel.log')
    return
  }

  logger.info('')
  logger.info('OpenClaw is now running inside CLOVE kernel sandbox.')
  logger.info('')
  logger.info('What CLOVE is doing:')
  logger.info('  ✓ All shell commands audited and permission-gated')
  logger.info('  ✓ File access restricted by Landlock ACLs')
  logger.info('  ✓ Network limited to allowed domains')
  logger.info('  ✓ LLM calls cost-tracked with budget enforcement')
  logger.info('  ✓ PII automatically scanned/redacted on every prompt')
  logger.info('  ✓ Full audit trail (every action logged)')
  logger.info('')
  logger.info('Next steps:')
  logger.info('  openclaw clove status     # check health + cost')
  logger.info('  openclaw clove fleet      # run multiple agents')
  logger.info(`  open http://localhost:${cfg.apiPort}/dashboard  # live dashboard`)
}
