#!/bin/sh
set -eu

repo_root=$1
output=$2
manifest=$(find "${CARGO_HOME:-$HOME/.cargo}/registry/src" -path '*/cxxbridge-cmd-1.0.194/Cargo.toml' -print -quit)
if [ -z "$manifest" ]; then
  echo 'cxxbridge-cmd 1.0.194 source is unavailable; resolve the locked Cargo graph first' >&2
  exit 1
fi

mkdir -p "$output/rust"
cd "$repo_root"
mise exec -- cargo run --quiet --manifest-path "$manifest" -- crates/safeparts_desktop_bridge/src/bridge.rs -o "$output/bridge.rs.cc"
mise exec -- cargo run --quiet --manifest-path "$manifest" -- crates/safeparts_desktop_bridge/src/bridge.rs --header -o "$output/bridge.rs.h"
mise exec -- cargo run --quiet --manifest-path "$manifest" -- --header -o "$output/rust/cxx.h"
