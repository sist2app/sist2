# syntax=docker/dockerfile:1.7

#   docker buildx build --target artifact -o type=local,dest=. .          -> static linux binary
#   docker buildx build --target artifact-windows -o type=local,dest=. .  -> windows binary
#   docker buildx build .                                                 -> runtime image
#
# Two builds of the same source: the portable binary is static musl, the image is glibc. Mime
# detection spends most of its time in libmagic's regexes, and musl's TRE runs them ~1.5x slower
# than glibc does, which is worth ~1.5x on a scan of files without a usable extension.

FROM node:22-slim AS frontend

WORKDIR /build
COPY . .

RUN cd sist2-vue && npm install && npm run build
RUN cd sist2-admin && npm install && npm run build


FROM alpine:3.22 AS build-musl

RUN apk add --no-cache \
    build-base ninja-build git curl zip unzip tar pkgconf linux-headers bash \
    python3 py3-pip autoconf autoconf-archive automake libtool nasm yasm gettext-dev perl bison flex \
    texinfo gfortran \
    coreutils diffutils findutils grep gawk sed bc zlib-dev \
    && ln -sf /usr/lib/ninja-build/bin/ninja /usr/local/bin/ninja

# vcpkg on musl requires system binaries (the prebuilt vcpkg tool is glibc-only), and its
# port scripts need a newer cmake than Alpine packages, so take the musl wheel from PyPI
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
RUN pip install --break-system-packages --no-cache-dir cmake==4.4.2

RUN git clone https://github.com/microsoft/vcpkg /vcpkg \
    && git -C /vcpkg checkout 9e593bb18ea69cc5095e012465dcd675a822ed0d \
    && /vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build

# Dependencies first: this layer is the expensive one and only rebuilds when the manifest changes.
# The -release triplets skip the debug half of every port, which nothing in the image links against.
# --host-triplet too, or every build-time dependency is still built twice under the
# default x64-linux triplet.
ARG TARGETARCH
COPY vcpkg.json .
COPY overlay-ports overlay-ports
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    case "${TARGETARCH}" in \
        amd64) triplet=x64-linux-release ;; \
        arm64) triplet=arm64-linux-release ;; \
        *) echo "unsupported TARGETARCH: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && /vcpkg/vcpkg install --triplet="${triplet}" --host-triplet="${triplet}" \
        --x-manifest-root=/build --x-install-root=/build/vcpkg_installed

COPY . .

# The frontend is embedded into the binary, so it has to exist before cmake runs
COPY --from=frontend /build/sist2-vue/dist sist2-vue/dist

RUN case "${TARGETARCH}" in \
        amd64) platform=x64_linux_musl; triplet=x64-linux-release ;; \
        arm64) platform=arm64_linux_musl; triplet=arm64-linux-release ;; \
        *) echo "unsupported TARGETARCH: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && cmake -B build \
        "-DSIST_PLATFORM=${platform}" \
        "-DVCPKG_TARGET_TRIPLET=${triplet}" \
        "-DVCPKG_HOST_TRIPLET=${triplet}" \
        -DSIST_DEBUG=off \
        -DSIST_DEBUG_INFO=on \
        -DBUILD_TESTS=off \
        -DSIST_STATIC=on \
        -DVCPKG_INSTALLED_DIR=/build/vcpkg_installed \
        -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build -j "$(nproc)" \
    && strip build/sist2


FROM scratch AS artifact
COPY --from=build-musl /build/build/sist2 /sist2


# The Windows binary, cross-compiled with mingw-w64. Building it on Linux rather than a Windows
# runner keeps it on the same vcpkg machinery as the others, and both ffmpeg and mupdf take their
# portable code path when the host is not Windows.
FROM ubuntu:24.04 AS build-mingw

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential mingw-w64 mingw-w64-tools ninja-build git curl zip unzip tar pkg-config \
        ca-certificates python3 python3-pip autoconf autoconf-archive automake libtool libtool-bin \
        nasm yasm gettext perl bison flex texinfo bc \
    && rm -rf /var/lib/apt/lists/*

# Debian's mingw defaults to the win32 thread model, which ships no winpthreads. sist2, tesseract
# and leptonica all need pthreads.
RUN for tool in gcc g++; do \
        update-alternatives --set x86_64-w64-mingw32-${tool} /usr/bin/x86_64-w64-mingw32-${tool}-posix; \
    done

# Windows import libraries are lowercase in the sysroot. Ports that spell them with capitals
# (tesseract asks for -lWs2_32) resolve only on a case-insensitive filesystem.
RUN cd /usr/x86_64-w64-mingw32/lib \
    && for lib in ws2_32 wsock32 bcrypt crypt32 iphlpapi secur32 shlwapi userenv winmm psapi \
                  dbghelp mfplat mfuuid strmiids ole32 oleaut32 uuid gdi32 advapi32 shell32; do \
        capitalized=$(echo "${lib}" | sed 's/^\(.\)/\u\1/'); \
        if [ -e "lib${lib}.a" ] && [ ! -e "lib${capitalized}.a" ]; then \
            ln -s "lib${lib}.a" "lib${capitalized}.a"; \
        fi; \
    done \
    && ln -sf libws2_32.a libWS2_32.a

ENV VCPKG_FORCE_SYSTEM_BINARIES=1
RUN pip install --break-system-packages --no-cache-dir cmake==4.4.2

RUN git clone https://github.com/microsoft/vcpkg /vcpkg \
    && git -C /vcpkg checkout 9e593bb18ea69cc5095e012465dcd675a822ed0d \
    && /vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build

COPY vcpkg.json .
COPY overlay-ports overlay-ports
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    /vcpkg/vcpkg install --triplet=x64-mingw-static-release --host-triplet=x64-linux-release \
        --x-manifest-root=/build --x-install-root=/build/vcpkg_installed

COPY . .

COPY --from=frontend /build/sist2-vue/dist sist2-vue/dist

# BUILD_TESTS is off because nothing in this image would run the result. They do build for
# Windows (and pass under wine); only the ASan and UBSan variants are Linux-only.
RUN cmake -B build \
        -DSIST_PLATFORM=x64_windows \
        -DVCPKG_TARGET_TRIPLET=x64-mingw-static-release \
        -DVCPKG_HOST_TRIPLET=x64-linux-release \
        -DSIST_DEBUG=off \
        -DSIST_DEBUG_INFO=on \
        -DBUILD_TESTS=off \
        -DSIST_STATIC=on \
        -DVCPKG_INSTALLED_DIR=/build/vcpkg_installed \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/build/scripts/mingw-w64-x86_64.cmake \
        -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build -j "$(nproc)" \
    && x86_64-w64-mingw32-strip build/sist2.exe


FROM scratch AS artifact-windows
COPY --from=build-mingw /build/build/sist2.exe /sist2.exe


# Same steps against glibc. The base matches the runtime image, so the binary links against the
# libc it will run on.
FROM ubuntu:24.04 AS build-glibc

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ninja-build git curl zip unzip tar pkg-config ca-certificates \
        python3 python3-pip autoconf autoconf-archive automake libtool nasm yasm gettext perl \
        bison flex texinfo gfortran bc zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Same cmake as the musl build, and vcpkg builds its own tool rather than downloading one
ENV VCPKG_FORCE_SYSTEM_BINARIES=1
RUN pip install --break-system-packages --no-cache-dir cmake==4.4.2

RUN git clone https://github.com/microsoft/vcpkg /vcpkg \
    && git -C /vcpkg checkout 9e593bb18ea69cc5095e012465dcd675a822ed0d \
    && /vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build

ARG TARGETARCH
COPY vcpkg.json .
COPY overlay-ports overlay-ports
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    case "${TARGETARCH}" in \
        amd64) triplet=x64-linux-release ;; \
        arm64) triplet=arm64-linux-release ;; \
        *) echo "unsupported TARGETARCH: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && /vcpkg/vcpkg install --triplet="${triplet}" --host-triplet="${triplet}" \
        --x-manifest-root=/build --x-install-root=/build/vcpkg_installed

COPY . .

COPY --from=frontend /build/sist2-vue/dist sist2-vue/dist

# No SIST_STATIC: a fully static glibc binary fails to link (the ifunc references in static
# libm), so every dependency but libc itself is still linked in statically.
RUN case "${TARGETARCH}" in \
        amd64) platform=x64_linux; triplet=x64-linux-release ;; \
        arm64) platform=arm64_linux; triplet=arm64-linux-release ;; \
        *) echo "unsupported TARGETARCH: ${TARGETARCH}" >&2; exit 1 ;; \
    esac \
    && cmake -B build \
        "-DSIST_PLATFORM=${platform}" \
        "-DVCPKG_TARGET_TRIPLET=${triplet}" \
        "-DVCPKG_HOST_TRIPLET=${triplet}" \
        -DSIST_DEBUG=off \
        -DSIST_DEBUG_INFO=on \
        -DBUILD_TESTS=off \
        -DVCPKG_INSTALLED_DIR=/build/vcpkg_installed \
        -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build -j "$(nproc)" \
    && strip build/sist2


FROM ubuntu:24.04 AS runtime

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV SIST2_BINARY=/root/sist2
ENV DATA_FOLDER=/sist2-admin/

WORKDIR /root
ENTRYPOINT ["/root/sist2"]

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates curl git python3 python3-pip \
    && curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# sist2 links tesseract statically, so only the trained models are needed at runtime.
# /usr/share/tessdata is the first path checked by src/cli.c.
ARG TESSDATA_LANGS="chi_sim deu eng equ fra hin jpn osd pol rus spa"
RUN mkdir -p /usr/share/tessdata && cd /usr/share/tessdata && \
    for lang in ${TESSDATA_LANGS}; do \
        curl -fsSL -O "https://raw.githubusercontent.com/tesseract-ocr/tessdata/main/${lang}.traineddata"; \
    done

# python is for user scripts
RUN ln -sf /usr/bin/python3 /usr/bin/python && \
    python -m pip install --no-cache-dir --break-system-packages \
        git+https://github.com/sist2app/sist2-python.git@2.1

COPY --from=build-glibc /build/build/sist2 /root/sist2
COPY --from=frontend /build/sist2-admin/server/ /root/sist2-admin/server/
COPY --from=frontend /build/sist2-admin/frontend/dist/ /root/sist2-admin/frontend/dist/
