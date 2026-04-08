# CLOVE — Multi-User Architecture

> MVP target: shared kernel, workspace-scoped API keys, Supabase Auth

---

## Current State (Single-Tenant)

One kernel serves one operator. `CLOVE_API_KEY` is a single secret that grants full access. Anyone with the key can read/write all workspaces, agents, runs, and memory. No user accounts, no isolation between users.

This is fine for an agency (you control everything) but not for a product where multiple customers need isolation.

---

## Target Architecture (Multi-Tenant MVP)

```
  ┌──────────────────────────────────────────┐
  │  Users                                    │
  │  user_a@example.com  user_b@example.com  │
  └───────────┬────────────────┬─────────────┘
              │ login          │ login
              ▼                ▼
  ┌───────────────────────────────────────────┐
  │  Supabase Auth                             │
  │  GitHub OAuth / email+password            │
  │  JWT issued per session                   │
  └───────────────────┬───────────────────────┘
                      │
              ┌───────▼────────┐
              │  Dashboard     │
              │  (Next.js)     │
              │                │
              │ Generate API   │
              │ key → stored   │
              │ in Supabase    │
              └───────┬────────┘
                      │ CLOVE_API_KEY (user-scoped)
                      ▼
  ┌───────────────────────────────────────────┐
  │  CLOVE Kernel (shared)                    │
  │                                           │
  │  API key lookup → resolve workspace_id    │
  │  All operations scoped to workspace       │
  │                                           │
  │  user_a key → workspace_a                 │
  │  user_b key → workspace_b                 │
  │  (workspaces never see each other)        │
  └───────────────────────────────────────────┘
```

---

## What Needs to Be Built

### 1. Supabase Auth

Add user authentication using Supabase Auth. Supports GitHub OAuth and email/password out of the box.

**New tables:**
```sql
-- User profiles (auto-created on signup)
create table profiles (
  id uuid references auth.users primary key,
  email text,
  created_at timestamptz default now()
);

-- API keys (one or more per user)
create table api_keys (
  id uuid primary key default gen_random_uuid(),
  user_id uuid references auth.users not null,
  workspace_id text not null,        -- maps to kernel workspace
  key_hash text not null unique,     -- bcrypt/sha256 of actual key
  key_prefix text not null,          -- e.g. "clove-" for display
  name text,                         -- user-given label
  created_at timestamptz default now(),
  last_used_at timestamptz,
  revoked boolean default false
);

-- RLS: users only see their own keys
alter table api_keys enable row level security;
create policy "own keys only" on api_keys
  for all using (auth.uid() = user_id);
```

---

### 2. API Key Generation (Dashboard)

User flow:
1. Log in → redirected to `/dashboard`
2. Go to **Settings → API Keys**
3. Click **Generate Key** → key displayed once, then only prefix shown
4. Copy key → paste into Claude Code MCP config

Key format: `clove-<workspace_id>-<random_32_chars>`

**Dashboard page** (`/dashboard/settings/api-keys`):
- List existing keys with name, created date, last used
- Generate new key button
- Revoke key button
- Copy-to-clipboard on generation (shown once)

---

### 3. Kernel: Workspace-Scoped Key Validation

Currently the kernel checks:
```cpp
if (auth != "Bearer " + api_key_) { return 401; }
```

For multi-user, this becomes a lookup:
```
incoming key → hash → query Supabase api_keys table → get workspace_id
```

Two options:

**Option A — Supabase lookup on every request (simplest)**
Kernel calls Supabase REST API to validate key and resolve workspace_id. Adds ~50ms latency. Acceptable for MVP.

**Option B — Local cache with TTL (better)**
Kernel caches key→workspace_id mappings in-memory with a 60s TTL. First call hits Supabase, subsequent calls are local. Revocation takes up to 60s to propagate.

Recommended: Option B for MVP.

**Kernel changes needed:**
- Replace single `api_key_` string with a `KeyCache` (map + TTL)
- On auth, resolve key → workspace_id
- Pass workspace_id into every API handler so all DB ops are scoped

---

### 4. Workspace Isolation in the Kernel

Every kernel operation that touches data needs workspace scoping. Currently some endpoints are global.

Endpoints that need scoping:
- `GET /api/agents` → filter by workspace_id
- `GET /api/jobs` → filter by workspace_id
- `POST /api/jobs` → set workspace_id from resolved key
- `GET /api/memory` → filter by workspace_id
- `GET /api/runs` → filter by workspace_id

Workspaces already exist as a concept in the kernel — the job queue and run engine accept `workspace_id`. The main change is enforcing it at the auth layer instead of trusting the client to pass it.

---

### 5. Dashboard Auth Guard

All `/dashboard/*` routes need to check for a valid Supabase session. Unauthenticated users get redirected to `/login`.

```tsx
// middleware.ts
import { createMiddlewareClient } from '@supabase/auth-helpers-nextjs'

export async function middleware(req) {
  const res = NextResponse.next()
  const supabase = createMiddlewareClient({ req, res })
  const { data: { session } } = await supabase.auth.getSession()
  if (!session && req.nextUrl.pathname.startsWith('/dashboard')) {
    return NextResponse.redirect(new URL('/login', req.url))
  }
  return res
}
```

---

## Phased Plan

### Phase 1 — Auth + Key Generation (Week 1)
- [ ] Add Supabase Auth to dashboard (GitHub OAuth + email)
- [ ] Login/signup page
- [ ] `api_keys` table + RLS in Supabase
- [ ] API Keys settings page in dashboard
- [ ] Dashboard middleware auth guard

### Phase 2 — Kernel Key Validation (Week 2)
- [ ] KeyCache in kernel (map + 60s TTL)
- [ ] Supabase key lookup on first use
- [ ] workspace_id resolved from key, scoped to all handlers
- [ ] Revocation propagates within TTL window

### Phase 3 — Workspace Isolation (Week 3)
- [ ] Enforce workspace_id on all job/agent/memory/run endpoints
- [ ] Supabase RLS on all tables (users see only their workspace data)
- [ ] Dashboard shows only current user's data

### Phase 4 — Polish (Week 4)
- [ ] Usage metrics per key (request count, cost, tokens)
- [ ] Key expiration dates
- [ ] Email on signup → auto-create workspace
- [ ] Onboarding flow: login → get key → copy MCP config

---

## What Doesn't Change

- The kernel binary stays the same — no per-user kernel instances
- Railway deployment stays the same — one kernel, one MCP server
- MCP URL stays the same — users just use different API keys
- Existing agency/single-tenant deployments still work with a single hardcoded key

---

## Cost Implications

Shared kernel = shared Railway bill. For MVP, charge users a flat monthly fee that covers Railway costs + margin. Per-user cost tracking already exists via `budget.record_cost()` scoped to workspace — plug that into a billing page later.
