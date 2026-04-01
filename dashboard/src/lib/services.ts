/**
 * Service Registry — definitions for all supported MCP services.
 * The dashboard generates connection UI from these definitions.
 * Adding a service = adding a definition here, not writing new components.
 */

export interface ServiceDef {
  id: string
  name: string
  description: string
  category: 'dev' | 'communication' | 'data' | 'productivity' | 'monitoring' | 'ai'
  mcp_package: string
  auth: {
    type: 'token' | 'oauth' | 'connection_string' | 'none'
    env_var: string
    label: string
    placeholder: string
    help_url: string
    help_text: string
    verify_url?: string
  }
  webhook?: {
    supported: boolean
    events: string[]
  }
  tools_preview: string[]
  popular: boolean
}

export const SERVICES: ServiceDef[] = [
  // ── Dev ────────────────────────────────────────────
  {
    id: 'github',
    name: 'GitHub',
    description: 'Repositories, pull requests, issues, code review, actions',
    category: 'dev',
    mcp_package: '@modelcontextprotocol/server-github',
    auth: {
      type: 'token',
      env_var: 'GITHUB_TOKEN',
      label: 'Personal Access Token',
      placeholder: 'ghp_xxxxxxxxxxxxxxxxxxxx',
      help_url: 'https://github.com/settings/tokens/new?scopes=repo,read:org',
      help_text: 'Create a PAT with repo scope. Click the link above to generate one.',
      verify_url: 'https://api.github.com/user',
    },
    webhook: { supported: true, events: ['pull_request', 'issues', 'push', 'issue_comment'] },
    tools_preview: ['list_repos', 'get_pull_request', 'create_issue', 'post_comment', 'get_file_contents'],
    popular: true,
  },
  {
    id: 'gitlab',
    name: 'GitLab',
    description: 'Repositories, merge requests, pipelines, CI/CD',
    category: 'dev',
    mcp_package: '@modelcontextprotocol/server-gitlab',
    auth: {
      type: 'token',
      env_var: 'GITLAB_TOKEN',
      label: 'Personal Access Token',
      placeholder: 'glpat-xxxxxxxxxxxxxxxxxxxx',
      help_url: 'https://gitlab.com/-/user_settings/personal_access_tokens',
      help_text: 'Create a PAT with api scope.',
    },
    tools_preview: ['list_projects', 'get_merge_request', 'create_issue'],
    popular: false,
  },
  {
    id: 'linear',
    name: 'Linear',
    description: 'Project management, issues, sprints, roadmaps',
    category: 'dev',
    mcp_package: '@modelcontextprotocol/server-linear',
    auth: {
      type: 'token',
      env_var: 'LINEAR_API_KEY',
      label: 'API Key',
      placeholder: 'lin_api_xxxxxxxxxxxxxxxxxxxx',
      help_url: 'https://linear.app/settings/api',
      help_text: 'Generate a personal API key from Linear settings.',
    },
    tools_preview: ['list_issues', 'create_issue', 'update_issue', 'list_projects'],
    popular: true,
  },

  // ── Communication ──────────────────────────────────
  {
    id: 'slack',
    name: 'Slack',
    description: 'Channels, messages, threads, notifications, users',
    category: 'communication',
    mcp_package: '@modelcontextprotocol/server-slack',
    auth: {
      type: 'token',
      env_var: 'SLACK_BOT_TOKEN',
      label: 'Bot Token',
      placeholder: 'xoxb-xxxxxxxxxxxx-xxxxxxxxxxxx-xxxxxxxxxxxx',
      help_url: 'https://api.slack.com/apps',
      help_text: 'Create a Slack app, add Bot Token Scopes (channels:read, chat:write, users:read), install to workspace.',
    },
    webhook: { supported: true, events: ['message', 'app_mention', 'reaction_added'] },
    tools_preview: ['send_message', 'list_channels', 'get_thread', 'search_messages'],
    popular: true,
  },
  {
    id: 'discord',
    name: 'Discord',
    description: 'Servers, channels, messages, reactions',
    category: 'communication',
    mcp_package: '@modelcontextprotocol/server-discord',
    auth: {
      type: 'token',
      env_var: 'DISCORD_BOT_TOKEN',
      label: 'Bot Token',
      placeholder: 'MTxxxxxxxxxxxxxxxxxxxxxxxx.xxxxxx.xxxxxxxxxxxxxxxxx',
      help_url: 'https://discord.com/developers/applications',
      help_text: 'Create an application, add a bot, copy the token.',
    },
    tools_preview: ['send_message', 'list_channels', 'get_messages'],
    popular: false,
  },
  {
    id: 'gmail',
    name: 'Gmail',
    description: 'Read, send, search emails. Labels and threads.',
    category: 'communication',
    mcp_package: '@modelcontextprotocol/server-google-gmail',
    auth: {
      type: 'oauth',
      env_var: 'GOOGLE_APPLICATION_CREDENTIALS',
      label: 'Google Credentials JSON',
      placeholder: 'Path to credentials.json',
      help_url: 'https://console.cloud.google.com/apis/credentials',
      help_text: 'Create OAuth credentials in Google Cloud Console. Download the JSON file.',
    },
    tools_preview: ['send_email', 'search_emails', 'get_thread', 'list_labels'],
    popular: true,
  },

  // ── Data ───────────────────────────────────────────
  {
    id: 'postgres',
    name: 'PostgreSQL',
    description: 'SQL queries, schema exploration, table management',
    category: 'data',
    mcp_package: '@modelcontextprotocol/server-postgres',
    auth: {
      type: 'connection_string',
      env_var: 'DATABASE_URL',
      label: 'Connection String',
      placeholder: 'postgresql://user:pass@host:5432/database',
      help_url: '',
      help_text: 'Standard PostgreSQL connection string. The MCP server connects read-only by default.',
    },
    tools_preview: ['query', 'list_tables', 'describe_table', 'list_schemas'],
    popular: true,
  },
  {
    id: 'sqlite',
    name: 'SQLite',
    description: 'Local database queries and management',
    category: 'data',
    mcp_package: '@modelcontextprotocol/server-sqlite',
    auth: {
      type: 'none',
      env_var: 'SQLITE_PATH',
      label: 'Database Path',
      placeholder: '/path/to/database.db',
      help_url: '',
      help_text: 'Path to your SQLite database file.',
    },
    tools_preview: ['query', 'list_tables', 'describe_table'],
    popular: false,
  },

  // ── Productivity ───────────────────────────────────
  {
    id: 'notion',
    name: 'Notion',
    description: 'Pages, databases, wikis, task management',
    category: 'productivity',
    mcp_package: '@modelcontextprotocol/server-notion',
    auth: {
      type: 'token',
      env_var: 'NOTION_TOKEN',
      label: 'Integration Token',
      placeholder: 'ntn_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx',
      help_url: 'https://www.notion.so/my-integrations',
      help_text: 'Create an internal integration. Share pages/databases with it.',
    },
    tools_preview: ['search', 'get_page', 'create_page', 'query_database'],
    popular: true,
  },
  {
    id: 'google-drive',
    name: 'Google Drive',
    description: 'Files, folders, documents, spreadsheets',
    category: 'productivity',
    mcp_package: '@modelcontextprotocol/server-gdrive',
    auth: {
      type: 'oauth',
      env_var: 'GOOGLE_APPLICATION_CREDENTIALS',
      label: 'Google Credentials',
      placeholder: 'Path to credentials.json',
      help_url: 'https://console.cloud.google.com/apis/credentials',
      help_text: 'Enable Drive API and create OAuth credentials.',
    },
    tools_preview: ['list_files', 'read_file', 'search_files'],
    popular: false,
  },

  // ── Monitoring ─────────────────────────────────────
  {
    id: 'pagerduty',
    name: 'PagerDuty',
    description: 'Incidents, alerts, on-call schedules, escalation',
    category: 'monitoring',
    mcp_package: '@modelcontextprotocol/server-pagerduty',
    auth: {
      type: 'token',
      env_var: 'PAGERDUTY_API_KEY',
      label: 'API Key',
      placeholder: 'u+xxxxxxxxxxxxxxxxxx',
      help_url: 'https://support.pagerduty.com/docs/api-access-keys',
      help_text: 'Create a REST API key from PagerDuty settings.',
    },
    webhook: { supported: true, events: ['incident.triggered', 'incident.resolved'] },
    tools_preview: ['list_incidents', 'acknowledge_incident', 'list_services'],
    popular: false,
  },
  {
    id: 'sentry',
    name: 'Sentry',
    description: 'Error tracking, crash reports, performance monitoring',
    category: 'monitoring',
    mcp_package: '@modelcontextprotocol/server-sentry',
    auth: {
      type: 'token',
      env_var: 'SENTRY_AUTH_TOKEN',
      label: 'Auth Token',
      placeholder: 'sntrys_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx',
      help_url: 'https://sentry.io/settings/account/api/auth-tokens/',
      help_text: 'Create an auth token with project:read scope.',
    },
    tools_preview: ['list_issues', 'get_issue', 'list_events'],
    popular: false,
  },

  // ── AI ─────────────────────────────────────────────
  {
    id: 'deepwiki',
    name: 'DeepWiki',
    description: 'AI-indexed codebase knowledge. Understands repo architecture.',
    category: 'ai',
    mcp_package: 'deepwiki-mcp',
    auth: {
      type: 'token',
      env_var: 'DEEPWIKI_API_KEY',
      label: 'API Key',
      placeholder: 'dw_xxxxxxxxxxxxxxxxxxxx',
      help_url: 'https://deepwiki.com/settings/api',
      help_text: 'Get an API key from DeepWiki settings.',
    },
    tools_preview: ['query_repo', 'get_architecture', 'find_references'],
    popular: true,
  },
  {
    id: 'filesystem',
    name: 'Filesystem',
    description: 'Local file read/write access for agents',
    category: 'data',
    mcp_package: '@modelcontextprotocol/server-filesystem',
    auth: {
      type: 'none',
      env_var: '',
      label: 'Allowed Directory',
      placeholder: '/path/to/project',
      help_url: '',
      help_text: 'The MCP server will only access files within this directory.',
    },
    tools_preview: ['read_file', 'write_file', 'list_directory', 'search_files'],
    popular: true,
  },
]

export const SERVICE_CATEGORIES = [
  { id: 'all', label: 'All' },
  { id: 'dev', label: 'Development' },
  { id: 'communication', label: 'Communication' },
  { id: 'data', label: 'Data' },
  { id: 'productivity', label: 'Productivity' },
  { id: 'monitoring', label: 'Monitoring' },
  { id: 'ai', label: 'AI' },
] as const
