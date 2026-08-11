#!/usr/bin/env bash

VCPKG_ROOT="/vcpkg"

(
  cd sist2-vue/
  npm install
  npm run build
) &

(
  cd sist2-admin/
  npm install
  npm run build
) &

wait

mkdir build
(
  cd build
  cmake -DSIST_PLATFORM=arm64_linux -DSIST_DEBUG_INFO=on -DSIST_DEBUG=off -DBUILD_TESTS=off -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ..
  make -j $(nproc)
  strip sist2
)
mv build/sist2 sist2-arm64-linux

rm -rf CMakeFiles CMakeCache.txt
(
  cd build
  cmake -DSIST_PLATFORM=arm64_linux -DSIST_DEBUG_INFO=on -DSIST_DEBUG=on -DBUILD_TESTS=off -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ..
  make -j $(nproc)
)
mv build/sist2_debug sist2-arm64-linux-debug