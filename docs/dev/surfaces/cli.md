# CLI

Owner: `crates/safeparts/`
Nearest contract: [`crates/safeparts/AGENTS.md`](../../../crates/safeparts/AGENTS.md)

## What belongs here

The CLI is the script-friendly interface over `safeparts_core`. For scripts and CI pipelines, use the [CLI automation manual](../manuals/cli-automation.md).

Keep it:

- non-interactive by default
- stdin/stdout friendly
- clear about `-i -` and `-o -`
- consistent with established flags for threshold, share count, encoding, input, output, and passphrases

## Change rules

- Put cryptographic behavior in core, not in CLI glue.
- Map core errors to actionable CLI messages without echoing sensitive input.
- Prefer `--passphrase-file` examples over command-line passphrases in docs.
- Add or update black-box tests in `crates/safeparts/tests/e2e.rs` for user-visible behavior.
- Keep explicit `-i -` and `-o -` coverage for both split and combine so stdin/stdout routing remains script-safe.
- Reject output paths containing NUL before creating temporary files or replacing outputs.
- Update the CLI automation manual when flags, stdin/stdout behavior, exit behavior, or automation guidance changes.

## Useful checks

```bash
cargo test -p safeparts --test e2e
cargo test --all-features
cargo clippy --all-targets --all-features -- -D warnings
```

## When CLI changes

Update:

- [`docs/dev/feature-matrix.md`](../feature-matrix.md)
- [`docs/dev/manuals/cli-automation.md`](../manuals/cli-automation.md) for script or CI automation changes
- [`docs/dev/verification.md`](../verification.md) if commands change
- user docs only for released user-facing flag or behavior changes
