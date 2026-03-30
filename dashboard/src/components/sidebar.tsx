'use client'
import Link from 'next/link'
import { usePathname } from 'next/navigation'
import { useDemo } from '@/lib/demo-context'

const build = [
  { href: '/agents', label: 'Agents', icon: 'M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0z' },
  { href: '/pipelines', label: 'Pipelines', icon: 'M9 17V7m0 10a2 2 0 01-2 2H5a2 2 0 01-2-2V7a2 2 0 012-2h2a2 2 0 012 2m0 10a2 2 0 002 2h2a2 2 0 002-2M9 7a2 2 0 012-2h2a2 2 0 012 2m0 10V7m0 10a2 2 0 002 2h2a2 2 0 002-2V7a2 2 0 00-2-2h-2a2 2 0 00-2 2' },
  { href: '/connections', label: 'Connections', icon: 'M13.828 10.172a4 4 0 00-5.656 0l-4 4a4 4 0 105.656 5.656l1.102-1.101m-.758-4.899a4 4 0 005.656 0l4-4a4 4 0 00-5.656-5.656l-1.1 1.1' },
]

const operate = [
  { href: '/runs', label: 'Run', icon: 'M13 10V3L4 14h7v7l9-11h-7z' },
  { href: '/fleet', label: 'Fleet', icon: 'M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10' },
  { href: '/swarm', label: 'Swarm', icon: 'M12 6V4m0 2a2 2 0 100 4m0-4a2 2 0 110 4m-6 8a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4m6 6v10m6-2a2 2 0 100-4m0 4a2 2 0 110-4m0 4v2m0-6V4' },
]

const observe = [
  { href: '/activity', label: 'Activity', icon: 'M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2' },
  { href: '/settings', label: 'Settings', icon: 'M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.066 2.573c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.573 1.066c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.066-2.573c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z M15 12a3 3 0 11-6 0 3 3 0 016 0z' },
]

export function Sidebar() {
  const pathname = usePathname()
  const { isDemo, toggle } = useDemo()

  const isActive = (href: string) => {
    if (href === '/') return pathname === '/'
    return pathname.startsWith(href)
  }

  return (
    <aside className="fixed left-0 top-0 h-screen w-[220px] flex flex-col z-50" style={{ background: 'var(--bg-card)', borderRight: '1px solid var(--border)' }}>
      {/* Logo */}
      <Link href="/" className="flex items-center gap-2.5 px-5 pt-6 pb-5">
        <div className="w-8 h-8 rounded-lg flex items-center justify-center" style={{ background: 'linear-gradient(135deg, #e07628, #f59e0b)' }}>
          <span className="text-white text-[13px] font-bold">C</span>
        </div>
        <div>
          <div className="text-[15px] font-semibold tracking-[-0.02em]">CLOVE</div>
          <div className="text-[9px] tracking-[0.06em] uppercase" style={{ color: 'var(--text-dim)' }}>Agent Fleet OS</div>
        </div>
      </Link>

      <nav className="flex-1 px-3 overflow-y-auto">
        <SectionLabel>Build</SectionLabel>
        {build.map(item => <NavItem key={item.href} item={item} active={isActive(item.href)} />)}

        <SectionLabel>Operate</SectionLabel>
        {operate.map(item => <NavItem key={item.href} item={item} active={isActive(item.href)} />)}

        <SectionLabel>Observe</SectionLabel>
        {observe.map(item => <NavItem key={item.href} item={item} active={isActive(item.href)} />)}
      </nav>

      {/* Demo */}
      <div className="px-3 py-3" style={{ borderTop: '1px solid var(--border)' }}>
        <button onClick={toggle}
          className="w-full flex items-center justify-between px-3 py-2 rounded-lg text-[12px] transition-all"
          style={{ background: isDemo ? 'var(--yellow-light)' : 'transparent', color: isDemo ? 'var(--yellow)' : 'var(--text-dim)' }}>
          <span className="font-medium">{isDemo ? 'Exit Demo' : 'Demo Mode'}</span>
          <div className="w-[28px] h-[16px] rounded-full p-[2px] transition-all" style={{ background: isDemo ? 'var(--yellow)' : 'var(--border)' }}>
            <div className="w-[12px] h-[12px] rounded-full transition-all" style={{ background: isDemo ? '#fff' : 'var(--text-dim)', transform: isDemo ? 'translateX(12px)' : 'translateX(0)' }} />
          </div>
        </button>
      </div>
      <div className="px-5 py-2 text-[10px]" style={{ color: 'var(--text-dim)' }}>v2.0.0</div>
    </aside>
  )
}

function SectionLabel({ children }: { children: React.ReactNode }) {
  return <div className="text-[9px] font-semibold uppercase tracking-[0.1em] px-3 mt-5 mb-1.5" style={{ color: 'var(--text-dim)' }}>{children}</div>
}

function NavItem({ item, active }: { item: { href: string; label: string; icon: string }; active: boolean }) {
  return (
    <Link href={item.href}
      className="flex items-center gap-2.5 px-3 py-[7px] rounded-lg text-[13px] transition-all mb-[1px]"
      style={{ background: active ? 'var(--accent-light)' : 'transparent', color: active ? 'var(--accent)' : 'var(--text-secondary)', fontWeight: active ? 600 : 400 }}>
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: active ? 1 : 0.4 }}>
        <path d={item.icon} />
      </svg>
      {item.label}
    </Link>
  )
}
