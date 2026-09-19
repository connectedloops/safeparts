#!/bin/sh
set -eu

prototype_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$prototype_dir/build"

cmake -S "$prototype_dir" -B "$build_dir"
cmake --build "$build_dir"
exec "$build_dir/safeparts-ui-prototype" "$@"
