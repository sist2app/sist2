#!/usr/bin/env bash
set -e

(
  cd "$(dirname "$0")/.."
  rm -rf index.sist2

  python3 scripts/mime.py > src/parsing/mime_generated.c
  python3 scripts/serve_static.py > src/web/static_generated.c
  python3 scripts/index_static.py > src/index/static_generated.c
  python3 scripts/magic_static.py > src/magic_generated.c

  commit_hash=$(git rev-parse HEAD 2>/dev/null || echo unknown)
  printf "static const char *const Sist2CommitHash = \"%s\";\n" "$commit_hash" > src/git_hash.h
)