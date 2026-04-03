#!/bin/bash
# CLOVE v2 kernel launcher
# Secrets live in .env.kernel (gitignored) — copy .env.kernel.example to get started

set -a
[ -f "$(dirname "$0")/.env.kernel" ] && source "$(dirname "$0")/.env.kernel"
set +a

exec ./build/kernel/clove_kernel --api --sandbox --mcp "$@"
