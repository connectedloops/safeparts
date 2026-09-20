#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
build_dir="$repo_root/target/desktop-build"
cmake -S "$repo_root/apps/desktop" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$build_dir" --target safeparts-desktop -j2
exec "$build_dir/safeparts-desktop" "$@"
