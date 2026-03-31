'use client'
import Link from 'next/link'
import { usePathname } from 'next/navigation'
import { useDemo } from '@/lib/demo-context'

const nav = [
  { href: '/', label: 'Home', icon: 'M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-4 0h4' },
  { href: '/agents', label: 'Agents', icon: 'M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0z' },
  { href: '/activity', label: 'Activity', icon: 'M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2' },
  { href: '/settings', label: 'Settings', icon: 'M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.066 2.573c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.573 1.066c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.066-2.573c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z M15 12a3 3 0 11-6 0 3 3 0 016 0z' },
]

export function Sidebar() {
  const pathname = usePathname()
  const { isDemo, toggle } = useDemo()
  const isActive = (href: string) => href === '/' ? pathname === '/' : pathname.startsWith(href)

  return (
    <aside className="fixed left-0 top-0 h-screen w-[220px] flex flex-col z-50" style={{ background: 'var(--bg-card)', borderRight: '1px solid var(--border)' }}>
      <Link href="/" className="flex items-center gap-2.5 px-5 pt-6 pb-5">
        <div className="w-8 h-8 rounded-lg flex items-center justify-center" style={{ background: 'linear-gradient(135deg, #e07628, #f59e0b)' }}>
          <span className="text-white text-[13px] font-bold">C</span>
        </div>
        <div>
          <div className="text-[15px] font-semibold tracking-[-0.02em]">CLOVE</div>
          <div className="text-[9px] tracking-[0.06em] uppercase" style={{ color: 'var(--text-dim)' }}>Agent Fleet OS</div>
        </div>
      </Link>

      <nav className="flex-1 px-3">
        {nav.map(item => (
          <Link key={item.href} href={item.href}
            className="flex items-center gap-2.5 px-3 py-[9px] rounded-lg text-[13px] transition-all mb-[2px]"
            style={{ background: isActive(item.href) ? 'var(--accent-light)' : 'transparent', color: isActive(item.href) ? 'var(--accent)' : 'var(--text-secondary)', fontWeight: isActive(item.href) ? 600 : 400 }}>
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" style={{ opacity: isActive(item.href) ? 1 : 0.4 }}>
              <path d={item.icon} />
            </svg>
            {item.label}
          </Link>
        ))}
      </nav>

      {/* Kernel status */}
      <div className="px-4 py-2" style={{ borderTop: '1px solid var(--border)' }}>
        <div className="flex items-center gap-2 px-2 py-1.5">
          <div className="w-[6px] h-[6px] rounded-full animate-pulse" style={{ background: 'var(--green)' }} />
          <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>Kernel running</span>
        </div>
      </div>

      <div className="px-3 py-2">
        <button onClick={toggle} className="w-full flex items-center justify-between px-3 py-2 rounded-lg text-[11px] transition-all"
          style={{ background: isDemo ? 'var(--yellow-light)' : 'transparent', color: isDemo ? 'var(--yellow)' : 'var(--text-dim)' }}>
          <span className="font-medium">{isDemo ? 'Exit Demo' : 'Demo'}</span>
          <div className="w-[26px] h-[14px] rounded-full p-[2px] transition-all" style={{ background: isDemo ? 'var(--yellow)' : 'var(--border)' }}>
            <div className="w-[10px] h-[10px] rounded-full transition-all" style={{ background: isDemo ? '#fff' : 'var(--text-dim)', transform: isDemo ? 'translateX(12px)' : 'translateX(0)' }} />
          </div>
        </button>
      </div>
    </aside>
  )
}
