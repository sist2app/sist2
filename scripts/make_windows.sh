#!/usr/bin/env bash
# Build the Windows sist2 binary. Cross-compiled with mingw-w64, so this runs on a linux x64
# host rather than on Windows — same Dockerfile, same vcpkg manifest. CI does the same thing.

set -e
cd "$(dirname "$0")/.."

if ! docker buildx version > /dev/null 2>&1; then
  echo "docker buildx is required (apt install docker-buildx, or docker-buildx-plugin from Docker's repository)" >&2
  exit 1
fi

docker buildx build --target artifact-windows -o type=local,dest=./dist-windows .
mv "dist-windows/sist2.exe" "sist2-x64-windows.exe"
rm -rf dist-windows

echo "built sist2-x64-windows.exe"
