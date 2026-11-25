# Multi-stage production image:
# - Compiles headless native C++20 + Clipper2 katman binary with GCC 14.2 (-O3 -flto)
# - Runs zero-npm Node.js 22 server + native katman binary on matching Debian Trixie glibc!
FROM node:22-bookworm-slim AS nodebin

FROM debian:trixie-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY third_party ./third_party
COPY src/core ./src/core
COPY src/main.cpp ./src/main.cpp

RUN cmake -S /src -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKATMAN_ENABLE_VIEWER=OFF \
    && cmake --build /src/build --target katman -j$(nproc) \
    && strip /src/build/katman

FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=nodebin /usr/local/bin/node /usr/local/bin/node
COPY --from=builder /src/build/katman /usr/local/bin/katman

WORKDIR /app
COPY server ./server
COPY docs ./docs

RUN mkdir -p /app/server_data/jobs && chmod +x /usr/local/bin/katman

ENV NODE_ENV=production
ENV PORT=8098
ENV DOCS_DIR=/app/docs
ENV DATA_DIR=/app/server_data
ENV KATMAN_BIN=/usr/local/bin/katman

EXPOSE 8098
CMD ["node", "/app/server/server.mjs"]
