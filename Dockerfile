# syntax=docker/dockerfile:1

FROM debian:12-slim AS build

ARG TARGETARCH
ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_DISABLE_METRICS=1

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        curl \
        g++ \
        git \
        make \
        ninja-build \
        pkg-config \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN ./bootstrap-vcpkg.sh -disableMetrics

RUN arch="${TARGETARCH:-}" \
    && if [ -z "${arch}" ]; then \
        if command -v dpkg >/dev/null 2>&1; then arch="$(dpkg --print-architecture)"; else arch="$(uname -m)"; fi; \
    fi \
    && case "${arch}" in \
        arm64|aarch64) triplet=arm64-linux-release ;; \
        amd64|x86_64) triplet=x64-linux-release ;; \
        *) echo "Unsupported architecture: ${arch}" >&2; exit 1 ;; \
    esac \
    && ./vcpkg install \
        --triplet "${triplet}" \
        --host-triplet "${triplet}" \
        --overlay-triplets=/src/cmake/triplets

RUN arch="${TARGETARCH:-}" \
    && if [ -z "${arch}" ]; then \
        if command -v dpkg >/dev/null 2>&1; then arch="$(dpkg --print-architecture)"; else arch="$(uname -m)"; fi; \
    fi \
    && case "${arch}" in \
        arm64|aarch64) triplet=arm64-linux-release ;; \
        amd64|x86_64) triplet=x64-linux-release ;; \
        *) echo "Unsupported architecture: ${arch}" >&2; exit 1 ;; \
    esac \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/src/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_TARGET_TRIPLET="${triplet}" \
        -DVCPKG_HOST_TRIPLET="${triplet}" \
        -DVCPKG_OVERLAY_TRIPLETS=/src/cmake/triplets \
    && cmake --build build \
    && mkdir -p /opt/amarre/bin /opt/amarre/lib \
    && cp build/MqttToPubSubBridge /opt/amarre/bin/ \
    && find "vcpkg_installed/${triplet}/lib" -maxdepth 1 -type f \( -name "*.so" -o -name "*.so.*" \) -exec cp -a {} /opt/amarre/lib/ \;

FROM debian:12-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV AMARRE_CONFIG_FILE=/etc/amarre-v2/config.json
ENV LD_LIBRARY_PATH=/opt/amarre/lib

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libgcc-s1 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 65532 amarre \
    && useradd --system --uid 65532 --gid 65532 --home-dir /nonexistent --no-create-home --shell /usr/sbin/nologin amarre

COPY --from=build /opt/amarre /opt/amarre

USER amarre
ENTRYPOINT ["/opt/amarre/bin/MqttToPubSubBridge"]
