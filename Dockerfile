# syntax=docker/dockerfile:1.7

#   docker buildx build --target artifact -o type=local,dest=. .   -> static binary only
#   docker buildx build .                                          -> runtime image
FROM alpine:3.22 AS build

RUN apk add --no-cache \
    build-base ninja-build git curl zip unzip tar pkgconf linux-headers bash \
    python3 py3-pip autoconf autoconf-archive automake libtool nasm yasm gettext-dev perl bison flex \
    texinfo gfortran nodejs npm \
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

RUN cd sist2-vue && npm install && npm run build
RUN cd sist2-admin && npm install && npm run build

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
COPY --from=build /build/build/sist2 /sist2


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
    python -m pip install --no-cache --break-system-packages \
        git+https://github.com/sist2app/sist2-python.git@2.1

COPY --from=build /build/build/sist2 /root/sist2
COPY --from=build /build/sist2-admin/server/ /root/sist2-admin/server/
COPY --from=build /build/sist2-admin/frontend/dist/ /root/sist2-admin/frontend/dist/
