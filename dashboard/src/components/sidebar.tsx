'use client'
import Link from 'next/link'
import { usePathname } from 'next/navigation'
import { useDemo } from '@/lib/demo-context'

const main = [
  { href: '/', label: 'Dashboard', icon: 'M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-4 0h4' },
  { href: '/agents', label: 'Sandbox', icon: 'M9 12l2 2 4-4m5.618-4.016A11.955 11.955 0 0112 2.944a11.955 11.955 0 01-8.618 3.04A12.02 12.02 0 003 9c0 5.591 3.824 10.29 9 11.622 5.176-1.332 9-6.03 9-11.622 0-1.042-.133-2.052-.382-3.016z' },
  { href: '/worlds', label: 'Worlds', icon: 'M3.055 11H5a2 2 0 012 2v1a2 2 0 002 2 2 2 0 012 2v2.945M8 3.935V5.5A2.5 2.5 0 0010.5 8h.5a2 2 0 012 2 2 2 0 104 0 2 2 0 012-2h1.064M15 20.488V18a2 2 0 012-2h3.064M21 12a9 9 0 11-18 0 9 9 0 0118 0z' },
  { href: '/runs', label: 'Runs', icon: 'M13 10V3L4 14h7v7l9-11h-7z' },
  { href: '/fleet', label: 'Fleet', icon: 'M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0z' },
  { href: '/openclaw', label: 'OpenClaw', icon: 'M8 9l3 3-3 3m5 0h3M5 20h14a2 2 0 002-2V6a2 2 0 00-2-2H5a2 2 0 00-2 2v12a2 2 0 002 2z' },
  { href: '/memory', label: 'Memory', icon: 'M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4' },
  { href: '/mcp', label: 'MCP Bridge', icon: 'M13.828 10.172a4 4 0 00-5.656 0l-4 4a4 4 0 105.656 5.656l1.102-1.101m-.758-4.899a4 4 0 005.656 0l4-4a4 4 0 00-5.656-5.656l-1.1 1.1' },
]

const monitor = [
  { href: '/audit', label: 'Audit', icon: 'M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2' },
  { href: '/cost', label: 'Cost', icon: 'M12 8c-1.657 0-3 .895-3 2s1.343 2 3 2 3 .895 3 2-1.343 2-3 2m0-8c1.11 0 2.08.402 2.599 1M12 8V7m0 1v8m0 0v1m0-1c-1.11 0-2.08-.402-2.599-1M21 12a9 9 0 11-18 0 9 9 0 0118 0z' },
  { href: '/settings', label: 'Settings', icon: 'M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.066 2.573c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.573 1.066c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.066-2.573c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z M15 12a3 3 0 11-6 0 3 3 0 016 0z' },
]

export function Sidebar() {
  const pathname = usePathname()
  const { isDemo, toggle } = useDemo()

  return (
    <aside className="fixed left-0 top-0 h-screen w-[240px] flex flex-col z-50" style={{ background: 'var(--bg-card)', borderRight: '1px solid var(--border)' }}>
      {/* Logo */}
      <div className="px-6 pt-6 pb-5">
        <div className="flex items-center gap-2.5">
          <div className="w-8 h-8 rounded-lg flex items-center justify-center" style={{ background: 'linear-gradient(135deg, #e07628, #f59e0b)' }}>
            <span className="text-white text-[13px] font-bold">C</span>
          </div>
          <div>
            <div className="text-[15px] font-semibold tracking-[-0.02em]">CLOVE</div>
            <div className="text-[10px] tracking-[0.04em] uppercase" style={{ color: 'var(--text-dim)' }}>Agent Fleet OS</div>
          </div>
        </div>
      </div>

      {/* Main nav */}
      <nav className="flex-1 px-3 overflow-y-auto">
        <div className="text-[10px] font-medium uppercase tracking-[0.08em] px-3 mb-2" style={{ color: 'var(--text-dim)' }}>Main</div>
        {main.map((item) => <NavItem key={item.href} item={item} active={pathname === item.href} />)}

        <div className="text-[10px] font-medium uppercase tracking-[0.08em] px-3 mt-5 mb-2" style={{ color: 'var(--text-dim)' }}>Monitor</div>
        {monitor.map((item) => <NavItem key={item.href} item={item} active={pathname === item.href} />)}
      </nav>

      {/* Demo toggle + version */}
      <div className="px-4 py-3" style={{ borderTop: '1px solid var(--border)' }}>
        <button
          onClick={toggle}
          className="w-full flex items-center justify-between px-3 py-2 rounded-lg text-[12px] transition-all"
          style={{ background: isDemo ? 'var(--yellow-light)' : 'transparent', color: isDemo ? 'var(--yellow)' : 'var(--text-dim)' }}
        >
          <span className="font-medium">{isDemo ? 'Exit Demo' : 'Demo Mode'}</span>
          <div className="w-[30px] h-[16px] rounded-full p-[2px] transition-all" style={{ background: isDemo ? 'var(--yellow)' : 'var(--border)' }}>
            <div className="w-[12px] h-[12px] rounded-full transition-all" style={{ background: isDemo ? '#fff' : 'var(--text-dim)', transform: isDemo ? 'translateX(14px)' : 'translateX(0)' }} />
          </div>
        </button>
      </div>
      <div className="px-6 py-3 text-[11px]" style={{ color: 'var(--text-dim)' }}>
        v2.0.0
      </div>
    </aside>
  )
}

function NavItem({ item, active }: { item: { href: string; label: string; icon: string }; active: boolean }) {
  return (
    <Link
      href={item.href}
      className="flex items-center gap-3 px-3 py-[8px] rounded-lg text-[13px] transition-all mb-[2px]"
      style={{
        background: active ? 'var(--accent-light, #eeedfc)' : 'transparent',
        color: active ? 'var(--accent)' : 'var(--text-secondary)',
        fontWeight: active ? 500 : 400,
      }}
    >
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: active ? 1 : 0.5 }}>
        <path d={item.icon} />
      </svg>
      {item.label}
    </Link>
  )
}
