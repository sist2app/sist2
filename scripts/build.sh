#!/usr/bin/env bash

VCPKG_ROOT="/vcpkg"

(
  cd sist2-vue/
  npm install
  npm run build
) &

(
  cd sist2-admin/frontend/
  npm install
  npm run build
) &

wait

mkdir build
(
  cd build
  cmake -DSIST_PLATFORM=x64_linux -DSIST_DEBUG_INFO=on -DSIST_DEBUG=off -DBUILD_TESTS=off -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ..
  make -j $(nproc)
  strip sist2
  ./sist2 -v > VERSION
)
mv build/sist2 sist2-x64-linux

(
  cd build
  rm -rf CMakeFiles CMakeCache.txt
  cmake -DSIST_PLATFORM=x64_linux -DSIST_DEBUG_INFO=on -DSIST_DEBUG=on -DBUILD_TESTS=off -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ..
  make -j  $(nproc)
)
mv build/sist2_debug sist2-x64-linux-debug