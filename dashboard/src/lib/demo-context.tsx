'use client'
import { createContext, useContext, useState, type ReactNode } from 'react'

interface DemoContextType {
  isDemo: boolean
  toggle: () => void
}

const DemoContext = createContext<DemoContextType>({ isDemo: false, toggle: () => {} })

export function DemoProvider({ children }: { children: ReactNode }) {
  const [isDemo, setIsDemo] = useState(false)
  return (
    <DemoContext.Provider value={{ isDemo, toggle: () => setIsDemo((v) => !v) }}>
      {children}
    </DemoContext.Provider>
  )
}

export function useDemo() {
  return useContext(DemoContext)
}
