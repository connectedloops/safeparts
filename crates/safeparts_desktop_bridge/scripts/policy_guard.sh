#!/bin/sh
set -eu

cd "$(dirname "$0")/../../.."
package=crates/safeparts_desktop_bridge
bridge="$package/src/bridge.rs"
expected_bridge_sha='e7858e4a8aa3459bb5f3208fec7c0be659a1a972f00c61ec0788292107022731'
expected_sources="$package/src/bridge.rs
$package/src/lib.rs
$package/src/operation.rs
$package/tests/operation.rs"
tracked_sources=$(git ls-files "$package/*.rs" "$package/**/*.rs")
[ "$tracked_sources" = "$expected_sources" ] || {
  echo 'unexpected tracked desktop bridge Rust source set' >&2
  exit 1
}
[ ! -e "$package/build.rs" ] || { echo 'desktop bridge build.rs is forbidden' >&2; exit 1; }
actual_bridge_sha=$(shasum -a 256 "$bridge" | cut -d ' ' -f 1)
[ "$actual_bridge_sha" = "$expected_bridge_sha" ] || { echo 'bridge declaration changed without guard review' >&2; exit 1; }
[ "$(grep -R -l '#\[cxx::bridge\]' -- $tracked_sources | wc -l | tr -d ' ')" = 1 ] || {
  echo 'exactly one desktop cxx bridge declaration is required' >&2
  exit 1
}
[ "$(grep -R -l '#\[cxx::bridge\]' -- $tracked_sources)" = "$bridge" ] || {
  echo 'desktop cxx bridge declaration is in the wrong file' >&2
  exit 1
}

for source in "$package/src/lib.rs" "$package/src/operation.rs"; do
  sed '/^#!\[forbid(unsafe_code)\]$/d' "$source" |
    grep -En '\bunsafe\b|allow[[:space:]]*\([[:space:]]*unsafe_code|\bexpect[[:space:]]*\(|\bunwrap[[:space:]]*\(' && {
      echo "forbidden handwritten Rust construct in $source" >&2
      exit 1
    }
done

grep -Fq '#![forbid(unsafe_code)]' "$package/src/operation.rs" || {
  echo 'desktop bridge implementation must forbid unsafe code' >&2
  exit 1
}
[ "$(git ls-files '*Cargo.toml' | xargs grep -h -Ec '^unsafe_code = "allow"$' | awk '{ total += $1 } END { print total + 0 }')" = 1 ] || {
  echo 'exactly one package unsafe-code exception is permitted' >&2
  exit 1
}
grep -Fq 'unsafe_code = "forbid"' Cargo.toml || { echo 'workspace unsafe default must remain forbid' >&2; exit 1; }
grep -Fq 'cxx = "=1.0.194"' "$package/Cargo.toml" || { echo 'cxx pin missing' >&2; exit 1; }
grep -Fq 'cxx-build = "=1.0.194"' "$package/Cargo.toml" || { echo 'cxx-build pin missing' >&2; exit 1; }
echo DESKTOP_CXX_POLICY_GUARD_OK
