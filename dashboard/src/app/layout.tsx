import type { Metadata } from 'next'
import './globals.css'
import { Sidebar } from '@/components/sidebar'

export const metadata: Metadata = {
  title: 'CLOVE Dashboard',
  description: 'Agent fleet management',
}

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body className="flex min-h-screen">
        <Sidebar />
        <main className="flex-1 p-8 ml-56">{children}</main>
      </body>
    </html>
  )
}
