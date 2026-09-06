# Release builds

Safeparts publishes archives containing `safeparts` and `safeparts-tui` for Linux, Windows, Intel macOS, and Apple Silicon macOS. The web UI and help are deployed separately.

Tauri, native SwiftUI macOS, native WinUI Windows, and the UniFFI bridge are retired. Their source remains as dormant reference. Future releases do not build or publish GUI installers; historical releases remain unchanged.

## Release workflow controls

Third-party actions in `.github/workflows/release.yml` are pinned to reviewed commit SHAs with version comments. Rust and host runner inputs are fixed. The workflow defaults to read-only repository access; only the tag-only `publish` job receives `contents: write`.

Before changing a release pin, follow the review and SHA-resolution steps in [`docs/dev/surfaces/release.md`](../../docs/dev/surfaces/release.md), then run:

```bash
mise run workflow:check
```

This checks release pins and permissions, supported workload boundaries, platform archive coverage, and checksum safety. Release version validation reads only the active Rust crate manifests. Dormant manifests may need repair before they can be built again.

To exercise the multi-platform path without publishing a GitHub Release, dispatch the workflow from the branch containing the proposed pins:

```bash
gh workflow run release.yml --ref <branch> -f version=v0.3.1
```

The dispatch uploads a seven-day `safeparts-release-dry-run-v0.3.1` artifact after the platform archives are downloaded and checksummed. Assembly rejects duplicate asset names and empty archive sets. Tag pushes publish `.tar.gz`, `.zip`, and `SHA256SUMS.txt` only.

## CLI and TUI archives

From the repository root:

```bash
python3 scripts/release/check-version.py v0.3.1
cargo test --all-features
cargo build --release -p safeparts -p safeparts_tui
python3 scripts/release/package.py --version 0.3.1
```

On Windows, run the packaging command with `py -3` instead of `python3`.

`--version` controls the archive name. Active release manifests and binaries must already carry the matching project version. The packager reads binaries from `target/release`, or `target/<triple>/release` with `--target`, and writes to `dist/release` by default.
