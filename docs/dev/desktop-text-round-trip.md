# Experimental desktop text round trip

Issue 140 adds a reviewable native slice, not a desktop release. The accepted visual direction is [issue 140 comment 5747267572](https://github.com/connectedloops/safeparts/issues/140#issuecomment-5747267572). The source fixture remains available on `prototype/desktop-reference-ui` at `874c5e7`; production code does not import it.

## Run and test

Use synthetic text only.

```bash
mise run desktop:test
mise run desktop:run
```

The first command builds the Rust adapter and generated CXX source, compiles the Qt app with warnings denied, and runs the public CXX and Qt action suites. The second command builds and opens the same app.

Rust owns generated packets, retained recovery input, and authoritative recovered bytes. Qt receives bounded presentation copies. The generated interface uses consuming `destroy_operation`; CXX vectors own returned copies. Every fallible operation entry catches Rust unwinds and returns a content-free status. The only `unsafe_code = "allow"` exception belongs to generated `cxx::bridge` expansion and is pinned by `crates/safeparts_desktop_bridge/scripts/policy_guard.sh`; handwritten operation code remains `forbid(unsafe_code)`.

## Implemented slice

- Create exact UTF-8/LF text as V2 unprotected Words shares, with 2-of-3 defaults and valid threshold/share-count customization.
- Encode one selected share on demand and copy only that complete share.
- Retain bounded recovery paste batches in Rust, inspect all supplied shares on one worker, and recover only after the explicit action.
- Reject malformed, trailing, duplicate, mixed, unsupported, over-count, and over-limit input without filtering a subset.
- Reject stale success and error results by generation after edits, mode changes, Start over, and close.
- Preserve NUL, NBSP, U+2028, U+2029, supplementary-plane and composed/decomposed Unicode, whitespace, and leading/trailing newlines in the editor-owned model. The recovered widget is presentation only; Copy reads the authoritative Rust result.

Files, passphrases, other encodings, V1 input, save, print, packaging, and release-platform qualification remain outside this slice.

## Development-host evidence

Evidence was collected at commits based on `8183db0` on Apple-silicon macOS 15.5 (24F74), Qt 6.9.1 from `/opt/homebrew`, CMake 4.0.1, Apple Clang 17, Rust/Cargo 1.93.0 through `mise exec --`, and CXX 1.0.194. Qt 6.9.1 is only the installed development runtime; it does not replace the approved Qt 6.11 evaluation and packaging target.

Artifacts are generated under `target/desktop-evidence/`:

- `versions.txt`: exact host and tool versions.
- `network.csv`: five one-second, PID-filtered `nettop` samples during the synthetic Qt create/copy/recover/reset/close suite. The process had no TCP/UDP rows or bytes in those samples.
- `actions.stdout`: the six passing Qt test phases used during that observation.
- `closure.stderr` and `otool.txt`: loader and direct-link closure. The observed Qt images were Core, Gui, Widgets, DBus, the Cocoa platform plugin, the macOS style plugin, and Test for the test executable, all from Qt 6.9.1.
- `lsof-snapshots.txt`: supplemental open-file snapshots during the same flow. These snapshots are not write traces and cannot prove that deleted staging files were absent.
- `fs-usage.stderr`: the mandatory write-trace attempt. macOS rejected `fs_usage` because it requires root. No privilege elevation or host-security change was authorized, so process-attributed storage tracing remains blocked.

The public operation boundary test admitted a 1 MiB secret at 16 shares, rejected the next logical-volume step, exercised the 16 MiB paste, token, and 160 MiB retained-input bounds, and completed with a measured 324,468,736-byte maximum resident set for the Cargo test process. That measurement includes the test harness and is not a complete maximum-valid-workload Qt process capacity certificate. Full 1 GiB peak/private-footprint evidence, allocation-failure injection, disconnected repetition, marker write traces, Qt 6.11, and Windows/Linux platform rows remain unverified.

## TDD trace

- Red: `mise exec -- cargo test -p safeparts_core --test desktop_admission` failed because `inspect_share_set` did not exist (`/tmp/desktop140-core-red.log`). Green: the same test passed after core-owned inspection and version-preserving parsing were added.
- Red: `mise exec -- cargo test -p safeparts_desktop_bridge --test operation` failed because the operation module did not exist (`/tmp/desktop140-bridge-red.log`). Green: four public-interface tests now pass, including exact Unicode and admission boundaries.
- Red: strict CMake compilation first exposed mixed Qt 6.8/6.9 headers, then the Qt suite exposed an incorrect second create action. Green: CMake now binds headers to the selected Qt package and `ctest --test-dir target/desktop-build --output-on-failure` passes both generated-CXX and Qt action suites.
