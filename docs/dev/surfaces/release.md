# Release packaging

Owners: `scripts/release/`, `.github/workflows/release.yml`
Nearest contracts: [`scripts/AGENTS.md`](../../../scripts/AGENTS.md), root [`AGENTS.md`](../../../AGENTS.md)

## What belongs here

Release tooling builds and publishes:

- `safeparts` CLI archives
- `safeparts-tui` archives
- one checksum manifest listing every published asset by its release-page filename

CLI/TUI archives support Linux, macOS, and Windows. Retired Tauri, SwiftUI, WinUI, and dedicated UniFFI output are excluded from future releases. Historical releases remain unchanged.

The web UI is deployed as a tested, retained static artifact rather than a release archive. Netlify and Cloudflare consume the same artifact without rebuilding source. A manual `workflow_dispatch` assembles every release artifact and `SHA256SUMS.txt` into a short-lived dry-run artifact without creating a GitHub Release.

## Platform ownership

- `scripts/release/package.py` owns CLI/TUI archives.
- `.github/workflows/release.yml` joins these artifacts and creates the GitHub Release.

## Change rules

- Keep packaging deterministic in inputs, layout, naming, and validation.
- Do not embed secrets in scripts, archives, or logs.
- Keep artifact names stable unless a release task changes them deliberately.
- Update local commands, CI behavior, and release docs together.

## Pinned inputs and publication permissions

The release workflow uses immutable commit SHAs for third-party actions. Each `uses:` line keeps the reviewed action version beside the SHA. Build jobs use the Rust and Bun versions from `mise.toml` and fixed GitHub-hosted runner images.

Workflow permissions default to `contents: read`. Artifact assembly stays read-only. The separate `publish` job runs only for a pushed tag and is the only job granted `contents: write`. A manual dispatch still builds, validates, downloads, checksums, and uploads the full candidate, but it cannot create a GitHub Release.

`mise run workflow:check` tests this policy and runs actionlint. CI runs the policy tests whenever the release workflow, validator, or version sources change.

## Update a pin safely

1. Read the upstream release notes and confirm the new action or tool version supports the fixed runner image.
2. Resolve the reviewed tag to its commit SHA. For an annotated tag, use the dereferenced value from `git ls-remote <repository-url> 'refs/tags/<version>^{}'`.
3. For `dtolnay/rust-toolchain`, resolve `refs/heads/stable`, review that commit, and update the date in its comment. Keep `toolchain:` equal to the Rust version in `mise.toml`.
4. Update the SHA and version comment together. Keep Bun aligned with `mise.toml`.
5. Run `mise run workflow:check`, then start the release dry run and inspect the assembled artifact before merging.

Do not replace a SHA with a major tag, `stable`, `latest`, an `x` version, or a `*-latest` runner label.

## Useful checks

```bash
mise run workflow:check
cargo test --all-features
cargo build --release -p safeparts -p safeparts_tui
python3 scripts/release/package.py --version 0.3.1

# Full remote dry run; this assembles artifacts but does not publish a release.
gh workflow run release.yml --ref <branch> -f version=v0.3.1
gh run watch
```

Check archive layout and filenames, then verify `SHA256SUMS.txt` lists exactly the published assets. The remote dry run requires explicit authorization; local documentation checks do not require GitHub writes.

## When release behavior changes

Update:

- `scripts/release/README.md`
- [`docs/dev/verification.md`](../verification.md)
- [`docs/dev/feature-matrix.md`](../feature-matrix.md)
- supported CLI/TUI download guidance
- `.github/workflows/release.yml`
