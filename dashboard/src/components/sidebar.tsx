'use client'
import Link from 'next/link'
import { usePathname } from 'next/navigation'
import { useDemo } from '@/lib/demo-context'

const buildNav = [
  { href: '/agents', label: 'Agents', icon: 'M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0z' },
  { href: '/pipelines', label: 'Pipelines', icon: 'M9 17V7m0 10a2 2 0 01-2 2H5a2 2 0 01-2-2V7a2 2 0 012-2h2a2 2 0 012 2m0 10a2 2 0 002 2h2a2 2 0 002-2M9 7a2 2 0 012-2h2a2 2 0 012 2m0 10V7m0 10a2 2 0 002 2h2a2 2 0 002-2V7a2 2 0 00-2-2h-2a2 2 0 00-2 2' },
  { href: '/connections', label: 'Connections', icon: 'M13.828 10.172a4 4 0 00-5.656 0l-4 4a4 4 0 105.656 5.656l1.102-1.101m-.758-4.899a4 4 0 005.656 0l4-4a4 4 0 00-5.656-5.656l-1.1 1.1' },
  { href: '/templates', label: 'Templates', icon: 'M4 5a1 1 0 011-1h14a1 1 0 011 1v2a1 1 0 01-1 1H5a1 1 0 01-1-1V5zm0 8a1 1 0 011-1h6a1 1 0 011 1v6a1 1 0 01-1 1H5a1 1 0 01-1-1v-6zm10 0a1 1 0 011-1h4a1 1 0 011 1v6a1 1 0 01-1 1h-4a1 1 0 01-1-1v-6z' },
]

const operateNav = [
  { href: '/worlds', label: 'Worlds', icon: 'M3.055 11H5a2 2 0 012 2v1a2 2 0 002 2 2 2 0 012 2v2.945M8 3.935V5.5A2.5 2.5 0 0010.5 8h.5a2 2 0 012 2 2 2 0 104 0 2 2 0 012-2h1.064M15 20.488V18a2 2 0 012-2h3.064M21 12a9 9 0 11-18 0 9 9 0 0118 0z' },
  { href: '/fleet', label: 'Fleet & Swarms', icon: 'M19 11H5m14 0a2 2 0 012 2v6a2 2 0 01-2 2H5a2 2 0 01-2-2v-6a2 2 0 012-2m14 0V9a2 2 0 00-2-2M5 11V9a2 2 0 012-2m0 0V5a2 2 0 012-2h6a2 2 0 012 2v2M7 7h10' },
  { href: '/runs', label: 'Runs', icon: 'M13 10V3L4 14h7v7l9-11h-7z' },
  { href: '/audit', label: 'Audit', icon: 'M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z' },
  { href: '/settings', label: 'Settings', icon: 'M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.066 2.573c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.573 1.066c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.066-2.573c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z M15 12a3 3 0 11-6 0 3 3 0 016 0z' },
]

export function Sidebar() {
  const pathname = usePathname()
  const { isDemo, toggle } = useDemo()
  const isActive = (href: string) => href === '/' ? pathname === '/' : pathname.startsWith(href)

  return (
    <aside className="fixed left-0 top-0 h-screen w-[220px] flex flex-col z-50" style={{ background: 'var(--bg-card)', borderRight: '1px solid var(--border)' }}>
      <Link href="/" className="flex items-center gap-2.5 px-5 pt-5 pb-4">
        <div className="w-7 h-7 rounded-lg flex items-center justify-center" style={{ background: 'linear-gradient(135deg, #e07628, #f59e0b)' }}>
          <span className="text-white text-[12px] font-bold">C</span>
        </div>
        <div>
          <div className="text-[14px] font-semibold tracking-[-0.02em]">CLOVE</div>
          <div className="text-[9px] tracking-[0.06em] uppercase" style={{ color: 'var(--text-dim)' }}>Agent Fleet OS</div>
        </div>
      </Link>

      <nav className="flex-1 px-2.5 overflow-y-auto">
        <SL>Build</SL>
        {buildNav.map(i => <NI key={i.href} item={i} active={isActive(i.href)} />)}
        <SL>Operate</SL>
        {operateNav.map(i => <NI key={i.href} item={i} active={isActive(i.href)} />)}
      </nav>

      <div className="px-3 py-2.5" style={{ borderTop: '1px solid var(--border)' }}>
        <button onClick={toggle} className="w-full flex items-center justify-between px-2.5 py-1.5 rounded-lg text-[11px] transition-all"
          style={{ background: isDemo ? 'var(--yellow-light)' : 'transparent', color: isDemo ? 'var(--yellow)' : 'var(--text-dim)' }}>
          <span className="font-medium">{isDemo ? 'Exit Demo' : 'Demo Mode'}</span>
          <div className="w-[26px] h-[14px] rounded-full p-[2px] transition-all" style={{ background: isDemo ? 'var(--yellow)' : 'var(--border)' }}>
            <div className="w-[10px] h-[10px] rounded-full transition-all" style={{ background: isDemo ? '#fff' : 'var(--text-dim)', transform: isDemo ? 'translateX(12px)' : 'translateX(0)' }} />
          </div>
        </button>
      </div>
      <div className="px-5 py-2 text-[9px]" style={{ color: 'var(--text-dim)' }}>v2.0.0</div>
    </aside>
  )
}

function SL({ children }: { children: React.ReactNode }) {
  return <div className="text-[9px] font-semibold uppercase tracking-[0.1em] px-2.5 mt-4 mb-1" style={{ color: 'var(--text-dim)' }}>{children}</div>
}

function NI({ item, active }: { item: { href: string; label: string; icon: string }; active: boolean }) {
  return (
    <Link href={item.href} className="flex items-center gap-2 px-2.5 py-[6px] rounded-lg text-[12px] transition-all mb-[1px]"
      style={{ background: active ? 'var(--accent-light)' : 'transparent', color: active ? 'var(--accent)' : 'var(--text-secondary)', fontWeight: active ? 600 : 400 }}>
      <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: active ? 1 : 0.4 }}><path d={item.icon} /></svg>
      {item.label}
    </Link>
  )
}
