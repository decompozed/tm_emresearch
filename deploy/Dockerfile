# syntax=docker/dockerfile:1
# Production image of ToyMayhem: one image, three services (AuthServer, MainServer, CastServer).
# Build from the repository root:  docker build -f deploy/Dockerfile -t tm_emulator .
# The compose file starts one container per server from this image; the first argument picks the server.
# No MariaDB in here (it is its own container), no compiler, no debug tools, no root at run time.

# Optional seed for the vcpkg binary cache. The pipeline passes the restored cache with
#   --build-context vcpkg-seed=.cache/vcpkg-bin
# Without it this stage is empty and everything is compiled from source.
FROM scratch AS vcpkg-seed

FROM ubuntu:24.04@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-14 g++-14 cmake ninja-build git curl zip unzip tar pkg-config python3 \
        autoconf automake autoconf-archive libtool bison flex ca-certificates binutils \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-14 CXX=g++-14 VCPKG_DISABLE_METRICS=1 VCPKG_ROOT=/opt/vcpkg

# Same pinned vcpkg baseline as vcpkg.json and the pipeline
ARG VCPKG_BASELINE=4e08971f3ddc13018ca858a692efe92d3b6b9fce
RUN git clone -q https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout -q "$VCPKG_BASELINE" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
# Dependencies first, so this layer is reused as long as the manifest does not change
COPY vcpkg.json vcpkg-configuration.json ./
COPY vcpkg-overlays ./vcpkg-overlays
RUN --mount=type=bind,from=vcpkg-seed,target=/seed \
    mkdir -p /root/.cache/vcpkg/archives && cp -a /seed/. /root/.cache/vcpkg/archives/ \
    && "$VCPKG_ROOT/vcpkg" install --triplet x64-linux

COPY . /src
RUN cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    && cmake --build build --target AuthServer MainServer CastServer \
    && mkdir -p /out \
    && for s in AuthServer MainServer CastServer; do \
           f=$(find /src/Output /src -maxdepth 1 -name "$s.elf" | head -n1); \
           test -n "$f" || { echo "$s.elf not found"; exit 1; }; \
           strip --strip-debug "$f" && install -m 0555 "$f" /out/; \
       done \
    && ls -l /out \
    && for f in /out/*.elf; do echo "== $f"; ldd "$f"; if ldd "$f" | grep -q "not found"; then exit 1; fi; done

FROM ubuntu:24.04@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
# Only what the servers need: CA certificates for SMTP over TLS. bash is in the base image.
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/* \
    && groupadd --gid 10001 tm \
    && useradd --uid 10001 --gid 10001 --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin tm

# The servers read ../Setup/config.ini, ../RewardItemIDs.txt and ../ExternalLibraries relative to the Output folder
COPY --from=builder /out/ /app/emu/Output/
COPY ExternalLibraries/cgd_original /app/emu/ExternalLibraries/cgd_original
COPY RewardItemIDs.txt /app/emu/RewardItemIDs.txt
COPY deploy/docker-entrypoint.sh deploy/healthcheck.sh /app/
# /app/emu/Setup is a tmpfs at run time: the entrypoint writes config.ini there (the root file system is read only)
RUN mkdir -p /app/emu/Setup && chmod 0555 /app/docker-entrypoint.sh /app/healthcheck.sh \
    && chmod -R a-w /app && chown -R root:root /app

USER 10001:10001
WORKDIR /app/emu/Output
ENV TB_LOG_DIR=/var/log/toymayhem
EXPOSE 13000 13005 13006
ENTRYPOINT ["/app/docker-entrypoint.sh"]
CMD ["MainServer"]
