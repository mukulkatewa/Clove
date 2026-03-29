import { CloveClient } from "./client.js";

export interface AgentOptions { name: string; role?: string; socketPath?: string; pollInterval?: number }

export class Agent {
  readonly name: string
  readonly role: string
  readonly client: CloveClient
  private readonly pollInterval: number
  private tools: Map<string, (...args: unknown[]) => unknown> = new Map()
  private running = false

  constructor(opts: AgentOptions) {
    this.name = opts.name
    this.role = opts.role ?? ""
    this.client = new CloveClient(opts.socketPath ?? "/tmp/clove.sock")
    this.pollInterval = opts.pollInterval ?? 100
  }

  tool(name: string, fn: (...args: unknown[]) => unknown): this { this.tools.set(name, fn); return this }
  getTools(): string[] { return [...this.tools.keys()] }

  async run(): Promise<void> {
    await this.client.connect()
    await this.client.registerName(this.name)
    await this.client.hello()
    this.running = true
    const onSig = () => this.stop()
    process.on("SIGINT", onSig); process.on("SIGTERM", onSig)
    try {
      while (this.running) {
        const msgs = await this.client.recvMessages(10)
        if (Array.isArray(msgs)) for (const m of msgs) await this.onMessage(m)
        const evts = await this.client.pollEvents(10)
        if (Array.isArray(evts)) for (const e of evts) await this.onEvent(e)
        await this.onTick()
        await new Promise(r => setTimeout(r, this.pollInterval))
      }
    } finally { process.off("SIGINT", onSig); process.off("SIGTERM", onSig); this.client.disconnect() }
  }

  stop(): void { this.running = false }
  async think(prompt: string) { return this.client.think(prompt) }
  async store(key: string, value: unknown) { return this.client.store(key, value) }
  async fetch(key: string) { return this.client.fetch(key) }
  async emit(type: string, data?: Record<string, unknown>) { return this.client.emit(type, data) }
  async sendTo(id: number, content: string) { return this.client.sendMessage(id, content) }

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  protected async onMessage(_msg: unknown): Promise<void> {}
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  protected async onEvent(_event: unknown): Promise<void> {}
  protected async onTick(): Promise<void> {}
}
