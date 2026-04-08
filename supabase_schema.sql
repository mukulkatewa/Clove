-- CLOVE v2 — Supabase schema
-- Paste this into Supabase SQL Editor and run it.

-- Enable UUID extension
create extension if not exists "pgcrypto";

-- ── Workspaces ─────────────────────────────────────────────────────────────
create table if not exists workspaces (
    id          text        primary key default gen_random_uuid()::text,
    name        text        not null unique,
    status      text        not null default 'active',
    metadata    jsonb       not null default '{}',
    created_at  timestamptz not null default now(),
    updated_at  timestamptz not null default now()
);
create index if not exists idx_workspace_status on workspaces(status);

-- ── Agent definitions ──────────────────────────────────────────────────────
create table if not exists agent_definitions (
    name              text    primary key,
    workspace_id      text    references workspaces(id) on delete set null,
    description       text    not null default '',
    enabled           boolean not null default true,
    goal              text    not null default '',
    triggers          jsonb   not null default '[]',
    tools             jsonb   not null default '[]',
    connections       jsonb   not null default '[]',
    permissions       jsonb   not null default '{}',
    budget_per_run    numeric not null default 1.0,
    budget_daily_max  numeric not null default 10.0,
    budget_daily_spent numeric not null default 0.0,
    budget_last_reset timestamptz,
    model             text    not null default '',
    max_steps         integer not null default 20,
    created_at        timestamptz not null default now(),
    updated_at        timestamptz not null default now()
);
create index if not exists idx_agentdef_workspace on agent_definitions(workspace_id);
create index if not exists idx_agentdef_enabled   on agent_definitions(enabled);

-- ── Workspace data (input KV store per workspace) ─────────────────────────
create table if not exists workspace_data (
    id           text        primary key default gen_random_uuid()::text,
    workspace_id text        not null references workspaces(id) on delete cascade,
    key          text        not null,
    content_type text        not null default 'text/plain',
    content      text        not null default '',
    file_path    text        not null default '',
    created_at   timestamptz not null default now(),
    unique(workspace_id, key)
);
create index if not exists idx_wsdata_workspace on workspace_data(workspace_id);

-- ── Workspace outputs (agent results per workspace) ───────────────────────
create table if not exists workspace_outputs (
    id           text        primary key default gen_random_uuid()::text,
    workspace_id text        not null references workspaces(id) on delete cascade,
    agent_name   text        not null default '',
    run_id       text        not null default '',
    type         text        not null default 'text',
    title        text        not null default '',
    content      text        not null default '',
    cost_usd     numeric     not null default 0,
    created_at   timestamptz not null default now()
);
create index if not exists idx_wsoutput_workspace on workspace_outputs(workspace_id);
create index if not exists idx_wsoutput_run       on workspace_outputs(run_id);

-- ── Agent runs (run history) ───────────────────────────────────────────────
create table if not exists agent_runs (
    id           text        primary key default gen_random_uuid()::text,
    agent_name   text        not null default '',
    workspace_id text        references workspaces(id) on delete set null,
    goal         text        not null default '',
    status       text        not null default 'running',
    result       text        not null default '',
    steps        integer     not null default 0,
    cost_usd     numeric     not null default 0,
    model        text        not null default '',
    started_at   timestamptz not null default now(),
    completed_at timestamptz
);
create index if not exists idx_agentrun_workspace on agent_runs(workspace_id);
create index if not exists idx_agentrun_agent     on agent_runs(agent_name);
create index if not exists idx_agentrun_status    on agent_runs(status);

-- ── Swarms ─────────────────────────────────────────────────────────────────
create table if not exists swarms (
    name         text        primary key,
    workspace_id text        references workspaces(id) on delete set null,
    agents       jsonb       not null default '[]',
    goal         text        not null default '',
    budget       numeric     not null default 2.0,
    status       text        not null default 'idle',
    created_at   timestamptz not null default now()
);
create index if not exists idx_swarm_workspace on swarms(workspace_id);
create index if not exists idx_swarm_status    on swarms(status);

-- ── Row-level security (enable for all tables) ─────────────────────────────
alter table workspaces        enable row level security;
alter table agent_definitions enable row level security;
alter table workspace_data    enable row level security;
alter table workspace_outputs enable row level security;
alter table agent_runs        enable row level security;
alter table swarms            enable row level security;

-- Service role bypasses RLS automatically.
-- Allow anon/authenticated to read everything (tighten later per-user):
create policy "read_all" on workspaces        for select using (true);
create policy "read_all" on agent_definitions for select using (true);
create policy "read_all" on workspace_data    for select using (true);
create policy "read_all" on workspace_outputs for select using (true);
create policy "read_all" on agent_runs        for select using (true);
create policy "read_all" on swarms            for select using (true);

-- ── Realtime (subscribe to live updates in the dashboard) ─────────────────
-- Run this to enable realtime on key tables:
-- alter publication supabase_realtime add table agent_runs;
-- alter publication supabase_realtime add table workspace_outputs;
-- alter publication supabase_realtime add table agent_definitions;
