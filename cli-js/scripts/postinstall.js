#!/usr/bin/env node
/**
 * Postinstall script — builds the CLOVE kernel from source.
 * Runs automatically after `npm install -g @clove/cli`.
 *
 * Checks for: cmake, C++ compiler, openssl, curl, sqlite3
 * Builds the kernel binary and places it at ~/.clove/bin/clove_kernel
 */

import { execSync, execFileSync } from 'node:child_process'
import { existsSync, mkdirSync, copyFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { homedir, platform, cpus } from 'node:os'

const CLOVE_DIR = join(homedir(), '.clove')
const BIN_DIR = join(CLOVE_DIR, 'bin')
const KERNEL_PATH = join(BIN_DIR, 'clove_kernel')

// Colors
const g = (s) => `\x1b[32m${s}\x1b[0m`
const r = (s) => `\x1b[31m${s}\x1b[0m`
const c = (s) => `\x1b[36m${s}\x1b[0m`
const d = (s) => `\x1b[2m${s}\x1b[0m`
const b = (s) => `\x1b[1m${s}\x1b[0m`

function check(cmd, name) {
  try {
    execSync(`which ${cmd}`, { stdio: 'ignore' })
    return true
  } catch {
    console.log(r(`  Missing: ${name} (${cmd})`))
    return false
  }
}

function checkSilent(cmd) {
  try {
    execSync(`which ${cmd}`, { stdio: 'ignore' })
    return true
  } catch {
    return false
  }
}

function run() {
  console.log('')
  console.log(b(c('  CLOVE — Installing kernel')))
  console.log('')

  // Skip if kernel already exists
  if (existsSync(KERNEL_PATH)) {
    console.log(g('  Kernel already built at ') + d(KERNEL_PATH))
    console.log('')
    return
  }

  // Check prerequisites
  console.log('  Checking dependencies...')
  let ok = true
  ok = check('cmake', 'CMake') && ok
  ok = check('c++', 'C++ compiler') || check('g++', 'G++') || check('clang++', 'Clang++')

  if (platform() === 'darwin') {
    if (!check('brew', 'Homebrew')) {
      console.log(r('  Homebrew required on macOS. Install from https://brew.sh'))
      ok = false
    }
  }

  if (!ok) {
    console.log('')
    console.log(r('  Missing dependencies. Install them first:'))
    if (platform() === 'darwin') {
      console.log(d('    xcode-select --install'))
      console.log(d('    brew install cmake openssl curl sqlite3'))
    } else {
      // Detect Linux package manager
      const hasApt = checkSilent('apt')
      const hasDnf = checkSilent('dnf')
      const hasPacman = checkSilent('pacman')
      const hasZypper = checkSilent('zypper')
      if (hasApt) {
        console.log(d('    sudo apt install cmake g++ libcurl4-openssl-dev libssl-dev libsqlite3-dev'))
      } else if (hasDnf) {
        console.log(d('    sudo dnf install cmake gcc-c++ libcurl-devel openssl-devel sqlite-devel'))
      } else if (hasPacman) {
        console.log(d('    sudo pacman -S cmake gcc curl openssl sqlite'))
      } else if (hasZypper) {
        console.log(d('    sudo zypper install cmake gcc-c++ libcurl-devel libopenssl-devel sqlite3-devel'))
      } else {
        console.log(d('    Install: cmake, g++, libcurl-dev, openssl-dev, sqlite3-dev'))
      }
    }
    console.log('')
    console.log(d('  Then run: clove build'))
    return
  }

  console.log(g('  All dependencies found'))
  console.log('')

  // Find the source directory
  // When installed globally, the source is in the package directory
  const packageDir = resolve(import.meta.dirname, '..')
  const sourceDir = existsSync(join(packageDir, 'kernel')) ? packageDir
    : existsSync(join(packageDir, '..', 'kernel')) ? resolve(packageDir, '..')
    : null

  if (!sourceDir) {
    console.log(r('  Source not found. Building from git...'))
    buildFromGit()
    return
  }

  buildFromSource(sourceDir)
}

function buildFromSource(sourceDir) {
  const buildDir = join(sourceDir, 'build')
  const cores = cpus().length

  console.log(c('  Building kernel from source...'))
  console.log(d(`  Source: ${sourceDir}`))
  console.log(d(`  Cores: ${cores}`))
  console.log('')

  try {
    mkdirSync(buildDir, { recursive: true })

    console.log(d('  cmake ...'))
    execSync(`cmake ${sourceDir} -DCMAKE_BUILD_TYPE=Release`, {
      cwd: buildDir,
      stdio: 'inherit',
    })

    console.log('')
    console.log(d(`  cmake --build (${cores} cores)...`))
    execSync(`cmake --build . --target clove_kernel -j${cores}`, {
      cwd: buildDir,
      stdio: 'inherit',
    })

    // Copy binary to ~/.clove/bin/
    const builtBinary = join(buildDir, 'kernel', 'clove_kernel')
    if (existsSync(builtBinary)) {
      mkdirSync(BIN_DIR, { recursive: true })
      copyFileSync(builtBinary, KERNEL_PATH)
      execSync(`chmod +x ${KERNEL_PATH}`)

      console.log('')
      console.log(g(b('  Kernel installed!')))
      console.log(d(`  Binary: ${KERNEL_PATH}`))
      console.log('')
      console.log('  Run ' + c('clove start') + ' to begin.')
      console.log('')
    } else {
      console.log(r('  Build succeeded but binary not found'))
    }
  } catch (e) {
    console.log('')
    console.log(r('  Build failed.'))
    console.log(d(`  ${e.message || e}`))
    console.log('')
    console.log('  Try building manually:')
    console.log(d(`    cd ${sourceDir}`))
    console.log(d('    mkdir build && cd build'))
    console.log(d('    cmake .. -DCMAKE_BUILD_TYPE=Release'))
    const coresCmd = platform() === 'darwin' ? '$(sysctl -n hw.ncpu)' : '$(nproc)'
    console.log(d(`    cmake --build . -j${coresCmd}`))
  }
}

function buildFromGit() {
  const tmpDir = join(CLOVE_DIR, 'src')

  console.log(c('  Cloning CLOVE...'))
  try {
    if (!existsSync(tmpDir)) {
      execSync(`git clone --depth 1 --branch v2 https://github.com/aniiiiXD/Clove.git ${tmpDir}`, {
        stdio: 'inherit',
      })
    } else {
      execSync('git pull', { cwd: tmpDir, stdio: 'inherit' })
    }
    buildFromSource(tmpDir)
  } catch (e) {
    console.log(r(`  Clone failed: ${e.message || e}`))
  }
}

run()
