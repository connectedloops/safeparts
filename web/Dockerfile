# syntax=docker/dockerfile:1.7@sha256:a57df69d0ea827fb7266491f2813635de6f17269be881f696fbfdf2d83dda33e

# Build a self-contained image that serves the Safeparts Web UI and help site.
# Build from the repository root so Cargo.lock and the Rust crates are available.

ARG RUST_IMAGE=rust:1.93.0-slim-bookworm@sha256:776861219cd851131c1cec3bbd7cbeb16b99a794048097eb69ad9682a8ed0d57
ARG BUN_IMAGE=oven/bun:1.3.11-alpine@sha256:7ed9f74c326d1c260abe247ac423ccbf5ac92af62bb442d515d1f92f21e8ea9b
ARG NGINX_IMAGE=nginxinc/nginx-unprivileged:1.29.5-alpine@sha256:42a7d7f2ee23e9f5a1dcdf3647ba5c585bbd18f79e79cd817e70e8cd61c55779

ARG WASM_PACK_VERSION=0.15.0
ARG WASM_BINDGEN_CLI_VERSION=0.2.108

FROM ${RUST_IMAGE} AS rust-tools

ARG WASM_PACK_VERSION
ARG WASM_BINDGEN_CLI_VERSION

WORKDIR /work

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      pkg-config \
      libssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN rustup target add wasm32-unknown-unknown

RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/usr/local/cargo/git \
    cargo install wasm-pack --locked --version ${WASM_PACK_VERSION}

RUN --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/usr/local/cargo/git \
    cargo install wasm-bindgen-cli --locked --version ${WASM_BINDGEN_CLI_VERSION}

FROM rust-tools AS wasm-builder

WORKDIR /work

# The Web image needs only the core and WASM crates. This isolated workspace
# keeps Cargo metadata complete without staging unrelated workspace members.
COPY web/docker/wasm-workspace.toml ./Cargo.toml
COPY Cargo.lock ./Cargo.lock
COPY crates/safeparts_core ./crates/safeparts_core
COPY crates/safeparts_wasm ./crates/safeparts_wasm

RUN cargo metadata --locked --format-version 1 --no-deps > /dev/null

RUN --mount=type=cache,target=/work/target \
    --mount=type=cache,target=/usr/local/cargo/registry \
    --mount=type=cache,target=/usr/local/cargo/git \
    wasm-pack build crates/safeparts_wasm \
      --mode no-install \
      --target web \
      --release \
      --out-dir /out/wasm_pkg

FROM ${BUN_IMAGE} AS web-builder

ENV ASTRO_TELEMETRY_DISABLED=1

WORKDIR /work/web

COPY web/package.json web/bun.lock ./
RUN --mount=type=cache,target=/root/.bun \
    bun install --frozen-lockfile

COPY web/help/package.json web/help/bun.lock ./help/
WORKDIR /work/web/help
RUN --mount=type=cache,target=/root/.bun \
    bun install --frozen-lockfile

WORKDIR /work/web
COPY web/ ./
COPY --from=wasm-builder /out/wasm_pkg ./src/wasm_pkg

RUN bun run build
WORKDIR /work/web/help
RUN bun run build

FROM ${NGINX_IMAGE} AS runtime

COPY web/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=web-builder /work/web/dist /usr/share/nginx/html

USER 101

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget --quiet --tries=1 --spider http://127.0.0.1:8080/healthz || exit 1

EXPOSE 8080
