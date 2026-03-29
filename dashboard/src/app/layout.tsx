import type { Metadata } from 'next'
import './globals.css'
import { Sidebar } from '@/components/sidebar'
import { DemoProvider } from '@/lib/demo-context'

export const metadata: Metadata = { title: 'Clove', description: 'Agent Fleet OS' }

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body className="flex min-h-screen">
        <DemoProvider>
          <Sidebar />
          <main className="flex-1 py-8 px-10 ml-[240px]">{children}</main>
        </DemoProvider>
      </body>
    </html>
  )
}
