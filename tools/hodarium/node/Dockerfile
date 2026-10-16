# Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
# Build context: repository root. This file declares a build; it provisions no node.
FROM debian:trixie-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential ca-certificates cmake pkg-config python3 \
       qt6-base-dev qt6-websockets-dev \
       libicu-dev libsodium-dev libsqlite3-dev libgnutls28-dev \
       libmimalloc-dev libspdlog-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt LICENSE COPYING ./
COPY cmake/ cmake/
COPY src/ src/
COPY tools/generate-shipped-macro-labels.py tools/
COPY ATHENA/packages/ ATHENA/packages/
COPY ATHENA/styles/ ATHENA/styles/

RUN cmake -S . -B build_hodarium_node \
      -DATHENA_BUILD_HODARIUM_NODE_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build_hodarium_node --target athena-hodarium-node -j20 \
    && cmake --install build_hodarium_node --prefix /out

FROM debian:trixie-slim AS runtime
LABEL org.opencontainers.image.title="ATHENA Hodarium headless node" \
      org.opencontainers.image.licenses="GPL-3.0-or-later"

# Qt Network's OpenSSL backend and CA roots are needed for authority/Relay HTTPS.
# No Qt GUI/Widgets, display server, Secret Service, editor, or model runtime.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates libstdc++6 libqt6core6t64 libqt6network6 libqt6websockets6 \
       libssl3t64 libicu76 libsodium23 libsqlite3-0 libgnutls30t64 libmimalloc3 \
       libspdlog1.15 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 athena-hodarium-node \
    && useradd --uid 10001 --gid 10001 --no-create-home --no-log-init \
       --home-dir /var/lib/athena-hodarium-node --shell /usr/sbin/nologin \
       athena-hodarium-node \
    && install -d -m 0700 -o 10001 -g 10001 /var/lib/athena-hodarium-node /vault

COPY --from=build /out/bin/athena-hodarium-node /usr/local/bin/athena-hodarium-node
COPY LICENSE COPYING /usr/share/doc/athena-hodarium-node/

USER 10001:10001
ENV LANG=C.UTF-8
WORKDIR /var/lib/athena-hodarium-node
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/local/bin/athena-hodarium-node"]
# Nothing enrolls, initializes a Vault, or starts networking without an explicit command.
CMD ["--help"]
