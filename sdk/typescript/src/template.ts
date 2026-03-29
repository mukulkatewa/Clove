export interface TemplateMetadata { name: string; displayName: string; description: string; category: string; author: string; version: string; tags: string[] }
export interface TemplateSpec { mode: "single" | "fleet"; goal: string; model?: string; budget: number; max_steps: number; tools: string[]; agent_name?: string; agents?: number }
export interface TemplateParameter { name: string; type: string; label: string; placeholder?: string; required?: boolean; default?: string | number; options?: string[]; maps_to?: Record<string, Partial<TemplateSpec>> }
export interface AgentTemplate { apiVersion: string; kind: string; metadata: TemplateMetadata; spec: TemplateSpec; parameters: TemplateParameter[] }
export interface RunRequest { goal: string; model?: string; budget?: number; max_steps?: number; tools?: string[]; agent_name?: string }
export interface FleetRequest { goal: string; agents: number; budget?: number; model?: string }

export function resolveTemplate(template: AgentTemplate, params: Record<string, string>): RunRequest | FleetRequest {
  const spec = { ...template.spec }
  for (const p of template.parameters) {
    if (p.maps_to && params[p.name]) { const o = p.maps_to[params[p.name]]; if (o) Object.assign(spec, o) }
  }
  const goal = spec.goal.replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "")
  if (spec.mode === "fleet") return { goal, agents: spec.agents ?? 3, budget: spec.budget, model: spec.model }
  return { goal, model: spec.model, budget: spec.budget, max_steps: spec.max_steps, tools: spec.tools, agent_name: spec.agent_name }
}

export function validateParams(t: AgentTemplate, p: Record<string, string>): string[] {
  return t.parameters.filter(x => x.required && !p[x.name]).map(x => x.name)
}

export function getDefaults(t: AgentTemplate): Record<string, string> {
  const d: Record<string, string> = {}
  for (const p of t.parameters) { if (p.default !== undefined) d[p.name] = String(p.default) }
  return d
}
