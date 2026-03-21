#pragma once
//
// CLOVE Dashboard v2.0.0 — Embedded HTML/CSS
// Auto-served by the API server at /dashboard
//

#include <string>

namespace clove {

// ---------------------------------------------------------------------------
// CSS
// ---------------------------------------------------------------------------
inline const char* DASHBOARD_CSS = R"css(
/* CLOVE Dashboard v2.0.0 — Dark Theme */

:root {
    --bg-primary: #0d1117;
    --bg-secondary: #161b22;
    --bg-tertiary: #1c2128;
    --border: #30363d;
    --border-light: #3d444d;
    --text-primary: #c9d1d9;
    --text-secondary: #8b949e;
    --text-muted: #6e7681;
    --accent-blue: #58a6ff;
    --accent-green: #3fb950;
    --accent-red: #f85149;
    --accent-yellow: #d29922;
    --accent-purple: #bc8cff;
    --font-mono: 'JetBrains Mono', 'Fira Code', 'SF Mono', 'Cascadia Code', Consolas, monospace;
    --font-sans: -apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif;
    --radius: 6px;
    --shadow: 0 1px 3px rgba(0, 0, 0, 0.3), 0 1px 2px rgba(0, 0, 0, 0.4);
}

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

html, body {
    height: 100%;
    font-family: var(--font-sans);
    background: var(--bg-primary);
    color: var(--text-primary);
    line-height: 1.5;
    font-size: 14px;
    -webkit-font-smoothing: antialiased;
}

a { color: var(--accent-blue); text-decoration: none; }
a:hover { text-decoration: underline; }

.app { display: flex; flex-direction: column; min-height: 100vh; }

.header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 12px 24px; background: var(--bg-secondary);
    border-bottom: 1px solid var(--border); position: sticky; top: 0; z-index: 100;
}
.header-left { display: flex; align-items: center; gap: 16px; }
.header h1 { font-size: 18px; font-weight: 600; color: var(--text-primary); letter-spacing: -0.3px; }
.header h1 span { color: var(--accent-blue); }
.header-right { display: flex; align-items: center; gap: 12px; font-size: 12px; color: var(--text-secondary); }
.refresh-indicator { display: flex; align-items: center; gap: 6px; }
.refresh-dot {
    width: 8px; height: 8px; border-radius: 50%;
    background: var(--accent-green); animation: pulse 2s ease-in-out infinite;
}
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }

.nav {
    display: flex; gap: 2px; padding: 0 24px;
    background: var(--bg-secondary); border-bottom: 1px solid var(--border);
}
.nav a {
    padding: 10px 16px; font-size: 13px; font-weight: 500;
    color: var(--text-secondary); border-bottom: 2px solid transparent; transition: all 0.15s ease;
}
.nav a:hover { color: var(--text-primary); text-decoration: none; }
.nav a.active { color: var(--accent-blue); border-bottom-color: var(--accent-blue); }

.main { flex: 1; padding: 24px; max-width: 1400px; width: 100%; margin: 0 auto; }

.grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
.grid-sidebar { display: grid; grid-template-columns: 350px 1fr; gap: 20px; }
.full-width { grid-column: 1 / -1; }

.card {
    background: var(--bg-secondary); border: 1px solid var(--border);
    border-radius: var(--radius); box-shadow: var(--shadow); overflow: hidden;
}
.card-header {
    display: flex; align-items: center; justify-content: space-between;
    padding: 14px 18px; border-bottom: 1px solid var(--border); background: var(--bg-tertiary);
}
.card-header h2 {
    font-size: 13px; font-weight: 600; text-transform: uppercase;
    letter-spacing: 0.5px; color: var(--text-secondary);
}
.card-header .badge { font-size: 11px; padding: 2px 8px; border-radius: 10px; font-weight: 600; }
.card-body { padding: 18px; }
.card-body.no-pad { padding: 0; }

table { width: 100%; border-collapse: collapse; font-family: var(--font-mono); font-size: 12px; }
thead th {
    padding: 10px 14px; text-align: left; font-weight: 600; font-size: 11px;
    text-transform: uppercase; letter-spacing: 0.5px; color: var(--text-muted);
    border-bottom: 1px solid var(--border); background: var(--bg-tertiary); position: sticky; top: 0;
}
tbody td { padding: 10px 14px; border-bottom: 1px solid var(--border); color: var(--text-primary); vertical-align: middle; }
tbody tr:last-child td { border-bottom: none; }
tbody tr:hover { background: rgba(88, 166, 255, 0.04); }

.badge {
    display: inline-block; padding: 2px 10px; border-radius: 10px;
    font-size: 11px; font-weight: 600; font-family: var(--font-mono);
    text-transform: uppercase; letter-spacing: 0.3px;
}
.badge-running, .badge-ok { background: rgba(63, 185, 80, 0.15); color: var(--accent-green); border: 1px solid rgba(63, 185, 80, 0.3); }
.badge-paused, .badge-warning { background: rgba(210, 153, 34, 0.15); color: var(--accent-yellow); border: 1px solid rgba(210, 153, 34, 0.3); }
.badge-stopped, .badge-error, .badge-dead { background: rgba(248, 81, 73, 0.15); color: var(--accent-red); border: 1px solid rgba(248, 81, 73, 0.3); }
.badge-idle, .badge-info { background: rgba(88, 166, 255, 0.15); color: var(--accent-blue); border: 1px solid rgba(88, 166, 255, 0.3); }
.badge-security { background: rgba(248, 81, 73, 0.15); color: var(--accent-red); border: 1px solid rgba(248, 81, 73, 0.3); }
.badge-syscall { background: rgba(188, 140, 255, 0.15); color: var(--accent-purple); border: 1px solid rgba(188, 140, 255, 0.3); }
.badge-network { background: rgba(88, 166, 255, 0.15); color: var(--accent-blue); border: 1px solid rgba(88, 166, 255, 0.3); }
.badge-ipc, .badge-state_store, .badge-resource, .badge-world { background: rgba(210, 153, 34, 0.15); color: var(--accent-yellow); border: 1px solid rgba(210, 153, 34, 0.3); }
.badge-agent_lifecycle { background: rgba(63, 185, 80, 0.15); color: var(--accent-green); border: 1px solid rgba(63, 185, 80, 0.3); }

.metrics-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 14px; }
.metric { background: var(--bg-tertiary); border: 1px solid var(--border); border-radius: var(--radius); padding: 16px; text-align: center; }
.metric-value { font-family: var(--font-mono); font-size: 28px; font-weight: 700; color: var(--accent-blue); line-height: 1.2; }
.metric-label { font-size: 11px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.5px; margin-top: 4px; }
.metric.green .metric-value { color: var(--accent-green); }
.metric.yellow .metric-value { color: var(--accent-yellow); }
.metric.red .metric-value { color: var(--accent-red); }

.form-row { display: flex; gap: 10px; align-items: flex-end; }
.form-group { display: flex; flex-direction: column; gap: 4px; flex: 1; }
.form-group label { font-size: 11px; font-weight: 600; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.3px; }
input, select {
    background: var(--bg-primary); border: 1px solid var(--border); border-radius: var(--radius);
    padding: 8px 12px; color: var(--text-primary); font-family: var(--font-mono); font-size: 13px;
    outline: none; transition: border-color 0.15s ease;
}
input:focus, select:focus { border-color: var(--accent-blue); box-shadow: 0 0 0 2px rgba(88, 166, 255, 0.15); }
input::placeholder { color: var(--text-muted); }

button, .btn {
    display: inline-flex; align-items: center; gap: 6px; padding: 8px 16px;
    border-radius: var(--radius); font-size: 13px; font-weight: 500;
    border: 1px solid var(--border); background: var(--bg-tertiary);
    color: var(--text-primary); cursor: pointer; transition: all 0.15s ease; font-family: var(--font-sans);
}
button:hover, .btn:hover { background: var(--border); }
.btn-primary { background: var(--accent-blue); border-color: var(--accent-blue); color: #fff; }
.btn-primary:hover { background: #4c95e6; border-color: #4c95e6; }
.btn-danger { background: transparent; border-color: var(--accent-red); color: var(--accent-red); padding: 4px 10px; font-size: 11px; }
.btn-danger:hover { background: rgba(248, 81, 73, 0.15); }
.btn-sm { padding: 4px 10px; font-size: 11px; }

.filter-bar {
    display: flex; gap: 12px; align-items: center; padding: 14px 18px;
    border-bottom: 1px solid var(--border); background: var(--bg-tertiary); flex-wrap: wrap;
}
.filter-bar label { font-size: 11px; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.3px; }
.filter-bar select, .filter-bar input { font-size: 12px; padding: 5px 8px; }

.empty-state {
    display: flex; flex-direction: column; align-items: center;
    justify-content: center; padding: 48px 24px; color: var(--text-muted); font-size: 13px;
}

.htmx-indicator { display: none; }
.htmx-request .htmx-indicator { display: inline-block; }
.htmx-request.htmx-indicator { display: inline-block; }
.spinner {
    display: inline-block; width: 14px; height: 14px;
    border: 2px solid var(--border); border-top-color: var(--accent-blue);
    border-radius: 50%; animation: spin 0.6s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }

.scrollable { max-height: 420px; overflow-y: auto; }
.scrollable::-webkit-scrollbar { width: 6px; }
.scrollable::-webkit-scrollbar-track { background: var(--bg-secondary); }
.scrollable::-webkit-scrollbar-thumb { background: var(--border); border-radius: 3px; }
.scrollable::-webkit-scrollbar-thumb:hover { background: var(--border-light); }

.agent-item {
    display: flex; align-items: center; justify-content: space-between;
    padding: 12px 18px; border-bottom: 1px solid var(--border); cursor: default; transition: background 0.1s ease;
}
.agent-item:hover { background: rgba(88, 166, 255, 0.04); }
.agent-item:last-child { border-bottom: none; }
.agent-info { display: flex; flex-direction: column; gap: 2px; }
.agent-name { font-weight: 600; font-size: 13px; }
.agent-meta { font-size: 11px; color: var(--text-muted); font-family: var(--font-mono); }

.toggle-group { display: flex; align-items: center; gap: 8px; font-size: 12px; color: var(--text-secondary); }
.toggle-group input[type="checkbox"] { accent-color: var(--accent-blue); }

#spawn-result { margin-top: 12px; }
.spawn-success {
    padding: 10px 14px; background: rgba(63, 185, 80, 0.1); border: 1px solid rgba(63, 185, 80, 0.3);
    border-radius: var(--radius); color: var(--accent-green); font-family: var(--font-mono); font-size: 12px;
}
.spawn-error {
    padding: 10px 14px; background: rgba(248, 81, 73, 0.1); border: 1px solid rgba(248, 81, 73, 0.3);
    border-radius: var(--radius); color: var(--accent-red); font-family: var(--font-mono); font-size: 12px;
}

.ts { color: var(--text-muted); font-family: var(--font-mono); font-size: 11px; white-space: nowrap; }
.details-cell {
    max-width: 320px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    font-family: var(--font-mono); font-size: 11px; color: var(--text-secondary);
}

@media (max-width: 960px) {
    .grid-sidebar { grid-template-columns: 1fr; }
    .grid-2 { grid-template-columns: 1fr; }
    .main { padding: 16px; }
}
@media (max-width: 600px) {
    .header { padding: 10px 16px; }
    .nav { padding: 0 12px; overflow-x: auto; }
    .form-row { flex-direction: column; }
    .metrics-grid { grid-template-columns: repeat(2, 1fr); }
}
)css";

// ---------------------------------------------------------------------------
// Index (Overview) page
// ---------------------------------------------------------------------------
inline const char* DASHBOARD_INDEX_HTML = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CLOVE Dashboard</title>
    <link rel="stylesheet" href="/dashboard/style.css">
    <script src="https://unpkg.com/htmx.org@1.9.10"></script>
</head>
<body>
<div class="app">
    <header class="header">
        <div class="header-left">
            <h1><span>CLOVE</span> Dashboard v2.0.0</h1>
        </div>
        <div class="header-right">
            <div class="refresh-indicator">
                <div class="refresh-dot"></div>
                <span>Auto-refresh active</span>
            </div>
            <span class="ts" hx-get="/api/fragments/uptime" hx-trigger="load, every 5s" hx-swap="innerHTML">--</span>
        </div>
    </header>

    <nav class="nav">
        <a href="/dashboard" class="active">Overview</a>
        <a href="/dashboard/agents">Agents</a>
        <a href="/dashboard/audit">Audit</a>
        <a href="/dashboard/worlds">Worlds</a>
    </nav>

    <main class="main">
        <!-- Metrics -->
        <div class="card" style="margin-bottom: 20px;">
            <div class="card-header">
                <h2>System Metrics</h2>
                <span class="spinner htmx-indicator" id="metrics-spinner"></span>
            </div>
            <div class="card-body"
                 hx-get="/api/fragments/metrics"
                 hx-trigger="load, every 5s"
                 hx-swap="innerHTML"
                 hx-indicator="#metrics-spinner">
                <div class="empty-state"><div class="spinner"></div></div>
            </div>
        </div>

        <!-- Agent List + Spawn -->
        <div class="grid-sidebar" style="margin-bottom: 20px;">
            <div class="card">
                <div class="card-header">
                    <h2>Agents</h2>
                    <span class="badge badge-info"
                          hx-get="/api/fragments/agent-count"
                          hx-trigger="load, every 3s"
                          hx-swap="innerHTML">--</span>
                </div>
                <div class="card-body no-pad scrollable"
                     hx-get="/api/fragments/agents"
                     hx-trigger="load, every 3s"
                     hx-swap="innerHTML">
                    <div class="empty-state"><div class="spinner"></div></div>
                </div>
            </div>

            <div>
                <div class="card" style="margin-bottom: 20px;">
                    <div class="card-header"><h2>Spawn Agent</h2></div>
                    <div class="card-body">
                        <form hx-post="/api/agents" hx-target="#spawn-result" hx-swap="innerHTML" hx-ext="json-enc">
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Name</label>
                                    <input type="text" name="name" placeholder="my-agent" required>
                                </div>
                                <div class="form-group">
                                    <label>Command</label>
                                    <input type="text" name="command" placeholder="/path/to/script.py" required>
                                </div>
                                <button type="submit" class="btn btn-primary">Spawn</button>
                            </div>
                        </form>
                        <div id="spawn-result"></div>
                    </div>
                </div>

                <div class="card">
                    <div class="card-header"><h2>LLM Gateway</h2></div>
                    <div class="card-body"
                         hx-get="/api/fragments/inference"
                         hx-trigger="load, every 10s"
                         hx-swap="innerHTML">
                        <div class="empty-state"><div class="spinner"></div></div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Recent Audit -->
        <div class="card">
            <div class="card-header">
                <h2>Recent Audit Entries</h2>
                <a href="/dashboard/audit" class="btn btn-sm">View All</a>
            </div>
            <div class="card-body no-pad"
                 hx-get="/api/fragments/audit?limit=10"
                 hx-trigger="load, every 5s"
                 hx-swap="innerHTML">
                <div class="empty-state"><div class="spinner"></div></div>
            </div>
        </div>
    </main>
</div>
</body>
</html>
)html";

// ---------------------------------------------------------------------------
// Agents page
// ---------------------------------------------------------------------------
inline const char* DASHBOARD_AGENTS_HTML = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Agents - CLOVE Dashboard</title>
    <link rel="stylesheet" href="/dashboard/style.css">
    <script src="https://unpkg.com/htmx.org@1.9.10"></script>
</head>
<body>
<div class="app">
    <header class="header">
        <div class="header-left">
            <h1><span>CLOVE</span> Dashboard v2.0.0</h1>
        </div>
        <div class="header-right">
            <div class="refresh-indicator">
                <div class="refresh-dot"></div>
                <span>Auto-refresh active</span>
            </div>
            <span class="ts" hx-get="/api/fragments/uptime" hx-trigger="load, every 5s" hx-swap="innerHTML">--</span>
        </div>
    </header>

    <nav class="nav">
        <a href="/dashboard">Overview</a>
        <a href="/dashboard/agents" class="active">Agents</a>
        <a href="/dashboard/audit">Audit</a>
        <a href="/dashboard/worlds">Worlds</a>
    </nav>

    <main class="main">
        <div class="card" style="margin-bottom: 20px;">
            <div class="card-header"><h2>Spawn New Agent</h2></div>
            <div class="card-body">
                <form hx-post="/api/agents" hx-target="#spawn-result" hx-swap="innerHTML" hx-ext="json-enc">
                    <div class="form-row">
                        <div class="form-group">
                            <label>Agent Name</label>
                            <input type="text" name="name" placeholder="my-agent" required>
                        </div>
                        <div class="form-group">
                            <label>Command / Script Path</label>
                            <input type="text" name="command" placeholder="/path/to/script.py" required>
                        </div>
                        <button type="submit" class="btn btn-primary">Spawn Agent</button>
                    </div>
                </form>
                <div id="spawn-result"></div>
            </div>
        </div>

        <div class="card">
            <div class="card-header">
                <h2>All Agents</h2>
                <div style="display:flex; align-items:center; gap:12px;">
                    <span class="badge badge-info"
                          hx-get="/api/fragments/agent-count"
                          hx-trigger="load, every 3s"
                          hx-swap="innerHTML">--</span>
                    <span class="spinner htmx-indicator" id="agent-table-spinner"></span>
                </div>
            </div>
            <div class="card-body no-pad"
                 hx-get="/api/fragments/agent-table"
                 hx-trigger="load, every 3s"
                 hx-swap="innerHTML"
                 hx-indicator="#agent-table-spinner">
                <div class="empty-state"><div class="spinner"></div></div>
            </div>
        </div>
    </main>
</div>
</body>
</html>
)html";

// ---------------------------------------------------------------------------
// Audit page
// ---------------------------------------------------------------------------
inline const char* DASHBOARD_AUDIT_HTML = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Audit Log - CLOVE Dashboard</title>
    <link rel="stylesheet" href="/dashboard/style.css">
    <script src="https://unpkg.com/htmx.org@1.9.10"></script>
</head>
<body>
<div class="app">
    <header class="header">
        <div class="header-left">
            <h1><span>CLOVE</span> Dashboard v2.0.0</h1>
        </div>
        <div class="header-right">
            <div class="refresh-indicator" id="auto-refresh-indicator">
                <div class="refresh-dot"></div>
                <span>Auto-refresh active</span>
            </div>
            <span class="ts" hx-get="/api/fragments/uptime" hx-trigger="load, every 5s" hx-swap="innerHTML">--</span>
        </div>
    </header>

    <nav class="nav">
        <a href="/dashboard">Overview</a>
        <a href="/dashboard/agents">Agents</a>
        <a href="/dashboard/audit" class="active">Audit</a>
        <a href="/dashboard/worlds">Worlds</a>
    </nav>

    <main class="main">
        <div class="card">
            <div class="filter-bar">
                <label>Category</label>
                <select id="audit-category" onchange="applyFilters()">
                    <option value="">All</option>
                    <option value="SECURITY">Security</option>
                    <option value="AGENT_LIFECYCLE">Agent Lifecycle</option>
                    <option value="IPC">IPC</option>
                    <option value="STATE_STORE">State Store</option>
                    <option value="RESOURCE">Resource</option>
                    <option value="SYSCALL">Syscall</option>
                    <option value="NETWORK">Network</option>
                    <option value="WORLD">World</option>
                </select>

                <label>Limit</label>
                <select id="audit-limit" onchange="applyFilters()">
                    <option value="25">25</option>
                    <option value="50">50</option>
                    <option value="100" selected>100</option>
                    <option value="500">500</option>
                </select>

                <div style="flex:1;"></div>

                <div class="toggle-group">
                    <input type="checkbox" id="auto-refresh-toggle" checked onchange="toggleAutoRefresh()">
                    <label for="auto-refresh-toggle" style="text-transform:none;letter-spacing:0;">Auto-refresh</label>
                </div>

                <a href="/api/audit/export" class="btn btn-sm" target="_blank">Export JSONL</a>
            </div>

            <div class="card-body no-pad scrollable" style="max-height:640px;"
                 id="audit-table-container"
                 hx-get="/api/fragments/audit?limit=100"
                 hx-trigger="load, every 5s"
                 hx-swap="innerHTML">
                <div class="empty-state"><div class="spinner"></div></div>
            </div>
        </div>
    </main>
</div>

<script>
function applyFilters() {
    var cat = document.getElementById('audit-category').value;
    var limit = document.getElementById('audit-limit').value;
    var url = '/api/fragments/audit?limit=' + limit;
    if (cat) url += '&category=' + cat;
    var container = document.getElementById('audit-table-container');
    container.setAttribute('hx-get', url);
    htmx.trigger(container, 'htmx:abort');
    htmx.ajax('GET', url, {target: container, swap: 'innerHTML'});
}

function toggleAutoRefresh() {
    var enabled = document.getElementById('auto-refresh-toggle').checked;
    var container = document.getElementById('audit-table-container');
    var indicator = document.getElementById('auto-refresh-indicator');
    if (enabled) {
        var cat = document.getElementById('audit-category').value;
        var limit = document.getElementById('audit-limit').value;
        var url = '/api/fragments/audit?limit=' + limit;
        if (cat) url += '&category=' + cat;
        container.setAttribute('hx-trigger', 'load, every 5s');
        container.setAttribute('hx-get', url);
        indicator.style.display = 'flex';
        htmx.process(container);
    } else {
        container.setAttribute('hx-trigger', 'none');
        indicator.style.display = 'none';
        htmx.process(container);
    }
}
</script>
</body>
</html>
)html";

} // namespace clove
