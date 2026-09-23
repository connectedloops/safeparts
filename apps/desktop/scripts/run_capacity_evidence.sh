#!/bin/sh
set -eu

case_name=${1:-maximum-words}
case "$case_name" in
  maximum-words) test_name=maximum_words_split_keeps_every_share_exportable; runner=qt ;;
  maximum-valid) test_name=maximum_valid_workload_remains_bounded_and_resettable; runner=qt ;;
  maximum-recovery) test_name=maximum_base64_replacement_recovers_exact_bytes; runner=rust ;;
  maximum-protected) test_name=maximum_policy_argon2_recovery_executes_exactly; runner=rust ;;
  maximum-base58-codec) test_name=base58check::tests::maximum_codec_vector_round_trips_with_pinned_shape; runner=core ;;
  maximum-accessibility) test_name=maximum_accessibility_handoff_remains_bounded_and_responsive; runner=qt ;;
  matrix-base64) test_name=maximum_base64url_text_unprotected_recovers_exact_bytes; runner=rust ;;
  matrix-base58) test_name=maximum_base58check_binary_protected_recovers_exact_bytes; runner=rust ;;
  matrix-words) test_name=maximum_words_text_protected_recovers_exact_bytes; runner=rust ;;
  matrix-bip39) test_name=maximum_bip39_binary_unprotected_recovers_exact_bytes; runner=rust ;;
  *) echo "usage: $0 maximum-words|maximum-valid|maximum-recovery|maximum-protected|maximum-base58-codec|maximum-accessibility|matrix-base64|matrix-base58|matrix-words|matrix-bip39" >&2; exit 64 ;;
esac

[ "$(uname -s)" = Darwin ] || {
  echo 'desktop capacity evidence currently requires macOS /usr/bin/time -l metrics' >&2
  exit 1
}

repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
cd "$repo"
out="target/desktop-evidence/issue-147/capacity/$case_name"
rm -rf "$out"
mkdir -p "$out"

printf '{"phase":"build","case":"%s"}\n' "$case_name"
if [ "$runner" = qt ]; then
  cmake -S apps/desktop -B target/desktop-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    >"$out/build.stdout" 2>"$out/build.stderr"
  cmake --build target/desktop-build --target desktop-actions -j2 \
    >>"$out/build.stdout" 2>>"$out/build.stderr"
elif [ "$runner" = core ]; then
  mise exec -- cargo test --release -p safeparts_core --lib --no-run \
    >"$out/build.stdout" 2>"$out/build.stderr"
else
  mise exec -- cargo test --release -p safeparts_desktop_bridge --test operation --no-run \
    >"$out/build.stdout" 2>"$out/build.stderr"
fi

printf '{"phase":"run","case":"%s","test":"%s"}\n' "$case_name" "$test_name"
if [ "$runner" = qt ]; then
  /usr/bin/time -l env QT_QPA_PLATFORM=cocoa \
    target/desktop-build/desktop-actions "$test_name" \
    >"$out/test.stdout" 2>"$out/test.time"
else
  timeout_seconds=${CAPACITY_TIMEOUT_SECONDS:-300}
  python3 - "$timeout_seconds" "$out/test.stdout" "$out/test.time" "$test_name" "$runner" <<'PY'
import json
import os
import signal
import subprocess
import sys

seconds, stdout_path, stderr_path, test_name, runner = sys.argv[1:]
if runner == "core":
    target = ["-p", "safeparts_core", "--lib"]
else:
    target = ["-p", "safeparts_desktop_bridge", "--test", "operation"]
command = [
    "/usr/bin/time", "-l", "mise", "exec", "--", "cargo", "test", "--release",
    *target, test_name, "--", "--ignored", "--exact", "--nocapture",
]
with open(stdout_path, "w", encoding="utf-8") as stdout, open(stderr_path, "w", encoding="utf-8") as stderr:
    process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
    try:
        return_code = process.wait(timeout=int(seconds))
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        print(json.dumps({"phase": "timeout", "seconds": int(seconds), "test": test_name}), file=stderr)
        raise SystemExit(124)
raise SystemExit(return_code)
PY
fi

rss=$(awk '/maximum resident set size/{print $1}' "$out/test.time")
footprint=$(awk '/peak memory footprint/{print $1}' "$out/test.time")
[ -n "$rss" ] && [ -n "$footprint" ] || {
  echo 'required macOS memory metrics were not emitted' >&2
  exit 1
}
limit=1073741824
python3 - "$case_name" "$test_name" "$rss" "$footprint" "$limit" >"$out/summary.json" <<'PY'
import json, sys
case, test, rss, footprint, limit = sys.argv[1:]
rss, footprint, limit = map(int, (rss, footprint, limit))
print(json.dumps({
    "case": case,
    "test": test,
    "maximumResidentSetBytes": rss,
    "peakMemoryFootprintBytes": footprint,
    "limitBytes": limit,
    "rssPassed": rss < limit,
    "footprintPassed": footprint < limit,
}, sort_keys=True))
if rss >= limit or footprint >= limit:
    raise SystemExit(1)
PY
cat "$out/summary.json"
printf '{"phase":"complete","case":"%s"}\n' "$case_name"
