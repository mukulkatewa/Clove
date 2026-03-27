'use client'
import Link from 'next/link'
import { usePathname } from 'next/navigation'

const nav = [
  { href: '/', label: 'Overview', icon: '~' },
  { href: '/agents', label: 'Sandbox', icon: '|' },
  { href: '/runs', label: 'Runs', icon: '>' },
  { href: '/fleet', label: 'Fleet', icon: '#' },
  { href: '/openclaw', label: 'OpenClaw', icon: '@' },
  { href: '/audit', label: 'Audit', icon: '!' },
  { href: '/cost', label: 'Cost', icon: '$' },
  { href: '/settings', label: 'Settings', icon: '*' },
]

export function Sidebar() {
  const pathname = usePathname()

  return (
    <aside className="fixed left-0 top-0 h-screen w-56 border-r border-[var(--border)] bg-[var(--bg)] flex flex-col z-50">
      <div className="p-5 border-b border-[var(--border)]">
        <h1 className="text-lg font-bold tracking-wider" style={{ color: 'var(--accent)' }}>
          CLOVE
        </h1>
        <p className="text-xs mt-1" style={{ color: 'var(--text-dim)' }}>
          Agent Fleet OS
        </p>
      </div>
      <nav className="flex-1 py-3">
        {nav.map((item) => {
          const active = pathname === item.href
          return (
            <Link
              key={item.href}
              href={item.href}
              className={`flex items-center gap-3 px-5 py-2.5 text-sm transition-colors ${
                active
                  ? 'bg-[var(--accent-dim)] text-[var(--accent)] border-r-2 border-[var(--accent)]'
                  : 'text-[var(--text-dim)] hover:text-[var(--text)] hover:bg-[var(--bg-hover)]'
              }`}
            >
              <span className="mono w-4 text-center">{item.icon}</span>
              {item.label}
            </Link>
          )
        })}
      </nav>
      <div className="p-4 border-t border-[var(--border)] text-xs" style={{ color: 'var(--text-dim)' }}>
        v2.0.0
      </div>
    </aside>
  )
}
