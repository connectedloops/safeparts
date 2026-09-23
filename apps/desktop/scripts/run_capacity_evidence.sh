#!/bin/sh
set -eu

case_name=${1:-maximum-words}
case "$case_name" in
  maximum-words) test_name=maximum_words_split_keeps_every_share_exportable ;;
  maximum-valid) test_name=maximum_valid_workload_remains_bounded_and_resettable ;;
  *) echo "usage: $0 maximum-words|maximum-valid" >&2; exit 64 ;;
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
cmake -S apps/desktop -B target/desktop-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  >"$out/build.stdout" 2>"$out/build.stderr"
cmake --build target/desktop-build --target desktop-actions -j2 \
  >>"$out/build.stdout" 2>>"$out/build.stderr"

printf '{"phase":"run","case":"%s","test":"%s"}\n' "$case_name" "$test_name"
/usr/bin/time -l env QT_QPA_PLATFORM=cocoa \
  target/desktop-build/desktop-actions "$test_name" \
  >"$out/test.stdout" 2>"$out/test.time"

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
