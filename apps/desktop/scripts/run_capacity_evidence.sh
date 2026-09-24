#!/bin/sh
set -eu

case_name=${1:-maximum-words}
case "$case_name" in
  maximum-words) test_name=maximum_words_split_keeps_every_share_exportable; runner=qt ;;
  maximum-valid) test_name=maximum_valid_workload_remains_bounded_and_resettable; runner=qt ;;
  maximum-recovery) test_name=maximum_base64_replacement_recovers_exact_bytes; runner=rust ;;
  maximum-retained) test_name=exact_retained_recovery_limit_is_transactional_and_recovers_maximum_secret; runner=rust ;;
  maximum-protected) test_name=maximum_policy_argon2_recovery_executes_exactly; runner=rust ;;
  maximum-base58-codec) test_name=base58check::tests::maximum_codec_vector_round_trips_with_pinned_shape; runner=core ;;
  maximum-accessibility) test_name=maximum_accessibility_handoff_remains_bounded_and_responsive; runner=qt ;;
  matrix-base64) test_name=maximum_base64url_text_unprotected_recovers_exact_bytes; runner=rust ;;
  matrix-base58) test_name=maximum_base58check_binary_protected_recovers_exact_bytes; runner=rust ;;
  matrix-words) test_name=maximum_words_text_protected_recovers_exact_bytes; runner=rust ;;
  matrix-bip39) test_name=maximum_bip39_binary_unprotected_recovers_exact_bytes; runner=rust ;;
  *) echo "usage: $0 maximum-words|maximum-valid|maximum-recovery|maximum-retained|maximum-protected|maximum-base58-codec|maximum-accessibility|matrix-base64|matrix-base58|matrix-words|matrix-bip39" >&2; exit 64 ;;
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
  python3 - "$timeout_seconds" "$out/test.stdout" "$out/test.time" "$test_name" "$runner" "$case_name" "$out" <<'PY'
import json
import os
import signal
import re
import subprocess
import sys
import time

seconds, stdout_path, stderr_path, test_name, runner, case_name, out_dir = sys.argv[1:]
if runner == "core":
    target = ["-p", "safeparts_core", "--lib"]
else:
    target = ["-p", "safeparts_desktop_bridge", "--test", "operation"]
command = [
    "/usr/bin/time", "-l", "mise", "exec", "--", "cargo", "test", "--release",
    *target, test_name, "--", "--ignored", "--exact", "--nocapture",
]
def descendants(root_pid):
    result = []
    try:
        rows = subprocess.check_output(["ps", "-axo", "pid=,ppid=,comm="], text=True).splitlines()
    except subprocess.SubprocessError:
        return result
    children = {}
    commands = {}
    for row in rows:
        parts = row.strip().split(None, 2)
        if len(parts) != 3:
            continue
        pid, ppid = int(parts[0]), int(parts[1])
        children.setdefault(ppid, []).append(pid)
        commands[pid] = parts[2]
    pending = [root_pid]
    while pending:
        parent = pending.pop()
        for child in children.get(parent, []):
            result.append((child, commands.get(child, "")))
            pending.append(child)
    return result

sample_required = case_name == "maximum-retained"
samples = []
started = time.monotonic()
with open(stdout_path, "w", encoding="utf-8") as stdout, open(stderr_path, "w", encoding="utf-8") as stderr:
    process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
    return_code = None
    while return_code is None:
        if time.monotonic() - started > int(seconds):
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            print(json.dumps({"phase": "timeout", "seconds": int(seconds), "test": test_name}), file=stderr)
            raise SystemExit(124)
        return_code = process.poll()
        if sample_required:
            candidates = [(pid, command) for pid, command in descendants(process.pid)
                          if "operation-" in command]
            if candidates:
                pid, _ = candidates[-1]
                try:
                    rss_kib = int(subprocess.check_output(
                        ["ps", "-o", "rss=", "-p", str(pid)], text=True).strip())
                    footprint_output = subprocess.check_output(
                        ["/usr/bin/footprint", "-p", str(pid), "-f", "bytes"],
                        text=True, stderr=subprocess.STDOUT)
                    match = re.search(r"Footprint: ([0-9]+) B", footprint_output)
                    if match:
                        samples.append({"timestampMs": int((time.monotonic() - started) * 1000),
                                        "pid": pid, "rssBytes": rss_kib * 1024,
                                        "privateFootprintBytes": int(match.group(1))})
                except (subprocess.SubprocessError, ValueError):
                    pass
        if return_code is None:
            time.sleep(0.2)
if sample_required:
    with open(os.path.join(out_dir, "samples.jsonl"), "w", encoding="utf-8") as stream:
        for sample in samples:
            stream.write(json.dumps(sample, sort_keys=True) + "\n")
    with open(stderr_path, encoding="utf-8") as stream:
        phases = [line.strip() for line in stream if "capacity_phase=" in line]
    if not samples or not phases or any(sample["rssBytes"] <= 0 or sample["privateFootprintBytes"] <= 0 for sample in samples):
        print(json.dumps({"phase": "sampling_incomplete", "samples": len(samples),
                          "markers": len(phases)}), file=stderr)
        raise SystemExit(1)
    with open(os.path.join(out_dir, "phases.json"), "w", encoding="utf-8") as stream:
        json.dump(phases, stream, indent=2)
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
if [ "$case_name" = maximum-retained ]; then
  python3 - "$out" "$rss" "$footprint" >"$out/reconciliation.json" <<'PY'
import json, os, sys
out, time_rss, time_footprint = sys.argv[1:]
with open(os.path.join(out, "samples.jsonl"), encoding="utf-8") as stream:
    samples = [json.loads(line) for line in stream]
with open(os.path.join(out, "phases.json"), encoding="utf-8") as stream:
    phases = json.load(stream)
ledger = {
    "retainedRawRecoveryBytes": 160 * 1048576,
    "maximumSecretBytes": 1048576,
    "suppliedUniqueShares": 16,
    "bridgeCandidateAndTransportUpperBoundBytes": 2 * 160 * 1048576,
    "operationStateBudgetBytes": 256 * 1048576,
    "phaseWorkspaceBudgetBytes": 256 * 1048576,
    "uiRuntimeHeadroomBytes": 256 * 1048576,
    "argon2AcceptedMaximumBytes": 262144 * 1024,
    "visibilityLimit": "allocator internals and transient framework copies are not directly observable",
}
sampled_rss = max(item["rssBytes"] for item in samples)
sampled_private = max(item["privateFootprintBytes"] for item in samples)
result = {
    "sampleCount": len(samples),
    "sampledMaximumRssBytes": sampled_rss,
    "sampledMaximumPrivateFootprintBytes": sampled_private,
    "timeMaximumRssBytes": int(time_rss),
    "timeWrapperPeakMemoryFootprintBytes": int(time_footprint),
    "rssSamplingDeltaBytes": int(time_rss) - sampled_rss,
    "wrapperPrivateFootprintDeltaBytes": int(time_footprint) - sampled_private,
    "samplingLimit": "200 ms polling and process-start/exit races can miss short-lived peaks; time RSS is the peak cross-check, while sampled /usr/bin/footprint targets the actual test binary because time's wrapper footprint does not include its child",
    "phases": phases,
    "ownershipLedger": ledger,
}
required = ["capacity_phase=below_retained_complete", "capacity_phase=exact_retained_begin",
            "capacity_phase=exact_retained_recovered", "capacity_phase=above_limit",
            "capacity_phase=above_limit_preserved"]
if not all(any(marker in phase for phase in phases) for marker in required):
    raise SystemExit("required capacity phase marker missing")
limit = 1024 * 1048576
if max(sampled_rss, int(time_rss)) >= limit or sampled_private >= limit:
    raise SystemExit("sampled actual-process memory gate failed")
with open(os.path.join(out, "summary.json"), encoding="utf-8") as stream:
    summary = json.load(stream)
summary["peakMemoryFootprintBytes"] = sampled_private
summary["timeWrapperPeakMemoryFootprintBytes"] = int(time_footprint)
summary["maximumResidentSetBytes"] = max(sampled_rss, int(time_rss))
summary["rssPassed"] = summary["maximumResidentSetBytes"] < limit
summary["footprintPassed"] = sampled_private < limit
with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as stream:
    json.dump(summary, stream, sort_keys=True)
    stream.write("\n")
print(json.dumps(result, indent=2, sort_keys=True))
PY
fi
cat "$out/summary.json"
printf '{"phase":"complete","case":"%s"}\n' "$case_name"
