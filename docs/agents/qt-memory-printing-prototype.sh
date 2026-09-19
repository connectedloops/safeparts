#!/usr/bin/env bash
# THROWAWAY. Build dependencies only inside Docker; no real printer or real secrets.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
evidence=$(mktemp -d "${TMPDIR:-/tmp}/safeparts-qt-print.XXXXXX")
image=safeparts-qt-memory-print-prototype:local
printf 'Evidence: %s\n' "$evidence"
docker build -t "$image" -f "$here/qt-memory-printing-prototype.Dockerfile" "$here" \
    > "$evidence/build.log" 2>&1 || { tail -60 "$evidence/build.log"; exit 1; }
docker image inspect "$image" > "$evidence/image.json"
docker run --rm --network none --cap-add SYS_PTRACE \
    -v "$evidence:/evidence" "$image" 2>&1 | tee "$evidence/run.log"
printf 'Review summary.json and traces in %s\n' "$evidence"
