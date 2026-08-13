#!/usr/bin/env bash
# Build a fully static sist2 binary (musl) for the current architecture.
# Run on an x64 host for the x64 binary and on an arm64 host for the arm64
# binary — same Dockerfile, no emulation. CI does the same thing per-runner.

set -e
cd "$(dirname "$0")/.."

if ! docker buildx version > /dev/null 2>&1; then
  echo "docker buildx is required (apt install docker-buildx, or docker-buildx-plugin from Docker's repository)" >&2
  exit 1
fi

arch=$(uname -m)
case "$arch" in
  x86_64) name=x64 ;;
  aarch64) name=arm64 ;;
  *) echo "unsupported arch: $arch" >&2; exit 1 ;;
esac

docker buildx build --target artifact -o type=local,dest=./dist-static .
mv "dist-static/sist2" "sist2-${name}-linux-static"
rm -rf dist-static

echo "built sist2-${name}-linux-static"
