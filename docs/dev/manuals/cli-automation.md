# CLI automation manual

Use the `safeparts` CLI when a workflow needs shell-friendly split/combine behavior. Keep the CLI non-interactive in automation and keep secrets out of logs.

This manual is for contributor and operator automation. It does not replace a security review for production secret handling.

## Automation rules

- Use synthetic secrets in tests and CI drills.
- Do not enable shell tracing with `set -x` around commands that touch secrets, shares, or passphrases.
- Prefer files or stdin/stdout over command arguments for secret material.
- Prefer `--passphrase-file` over `--passphrase`.
- Write temporary files under a private directory with `umask 077`. When `--out` names a file, Safeparts writes through a temporary file and replaces the destination only after the write succeeds. On Unix, the resulting file is owner-only even if the process uses a broader umask.
- Do not store enough shares together in one CI secret store, artifact bucket, or vault path.
- Do not print recovered secrets. Compare files or pass bytes directly to the next controlled step.

## Command model

Split:

```bash
safeparts split \
  -k 2 \
  -n 3 \
  -e base64url \
  -i secret.bin \
  -o shares.txt
```

Combine:

```bash
safeparts combine \
  -i selected-shares.txt \
  -o recovered.bin
```

Useful flags:

| Flag | Applies to | Meaning |
| --- | --- | --- |
| `-k`, `--threshold` | split | Number of shares required to recover. |
| `-n`, `--shares` | split | Number of shares to create. |
| `-e`, `--encoding` | split, combine | Encoding for output or input. Combine can omit it and use auto detection. |
| `-i`, `--in` | split, combine | Read from a file. Use `-` for stdin. |
| `-o`, `--out` | split, combine | Write to a file. Use `-` for stdout. |
| `-P`, `--passphrase-file` | split, combine | Read passphrase from a file. Trailing newline is trimmed. |
| `-p`, `--passphrase` | split, combine | Passphrase as an argument. Avoid this in automation because shells and process tools may record it. |

Supported split encodings are `base64url`, `base58check`, `mnemo-words`, and `mnemo-bip39`. CLI aliases `base64` and `base58` are accepted.

## Local round-trip script

This script uses a synthetic secret and compares files without printing the recovered value.

```bash
#!/usr/bin/env bash
set -euo pipefail
umask 077

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

printf 'synthetic secret for automation test' > "$workdir/secret.bin"

safeparts split \
  -k 2 \
  -n 3 \
  -e base64url \
  -i "$workdir/secret.bin" \
  -o "$workdir/shares.txt"

sed -n '1,2p' "$workdir/shares.txt" > "$workdir/selected-shares.txt"

safeparts combine \
  -i "$workdir/selected-shares.txt" \
  -o "$workdir/recovered.bin"

cmp "$workdir/secret.bin" "$workdir/recovered.bin"
```

Use this pattern for synthetic smoke tests and operational drills. It does not validate a real backup. Before retiring a working copy or declaring a backup established, follow the [saved-backup checkpoint](../../../web/help/src/content/docs/it-devops-guide/break-glass.mdx#verify-saved-backup). It reads actual custody copies, exercises every Recovery share with the intended Passphrase protection, and compares recovered bytes silently. On failure, keep the known-good working copy and correct or recreate the backup before repeating verification.

The [synthetic saved-backup rehearsal](../../../web/help/src/content/docs/it-devops-guide/automation.mdx#saved-backup-rehearsal) covers success, wrong Passphrase, a valid backup of the wrong Secret, and correction through existing CLI commands. Run both locale examples with `scripts/dev/test_backup_rehearsal.py`; see the [help surface guide](../surfaces/help-docs.md).

## Passphrase-protected automation

Use a private passphrase file. Do not put the passphrase on the command line.

```bash
#!/usr/bin/env bash
set -euo pipefail
umask 077

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

printf 'synthetic protected secret' > "$workdir/secret.bin"
printf '%s' 'synthetic passphrase for tests' > "$workdir/passphrase.txt"

safeparts split \
  -k 2 \
  -n 3 \
  -e mnemo-words \
  -P "$workdir/passphrase.txt" \
  -i "$workdir/secret.bin" \
  -o "$workdir/shares.txt"

awk 'NR <= 2 { print }' "$workdir/shares.txt" > "$workdir/selected-shares.txt"

safeparts combine \
  -P "$workdir/passphrase.txt" \
  -i "$workdir/selected-shares.txt" \
  -o "$workdir/recovered.bin"

cmp "$workdir/secret.bin" "$workdir/recovered.bin"
```

For real passphrases, make the file come from your secret manager or operator input on a controlled host. Remove it at the end of the job.

## CI scenario 1: synthetic recovery drill

This is safe for regular CI because it does not use production secrets. It proves that the binary works and that split/combine still round-trips.

```yaml
name: safeparts synthetic recovery drill

on:
  schedule:
    - cron: '0 5 * * 1'
  workflow_dispatch:

jobs:
  recovery-drill:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install Rust
        uses: dtolnay/rust-toolchain@stable

      - name: Build CLI
        run: cargo build --release -p safeparts

      - name: Run synthetic drill
        shell: bash
        run: |
          set -euo pipefail
          umask 077

          workdir="$(mktemp -d)"
          trap 'rm -rf "$workdir"' EXIT

          printf 'synthetic CI drill secret' > "$workdir/secret.bin"

          target/release/safeparts split \
            -k 2 \
            -n 3 \
            -e base64url \
            -i "$workdir/secret.bin" \
            -o "$workdir/shares.txt"

          sed -n '1,2p' "$workdir/shares.txt" > "$workdir/selected-shares.txt"

          target/release/safeparts combine \
            -i "$workdir/selected-shares.txt" \
            -o "$workdir/recovered.bin"

          cmp "$workdir/secret.bin" "$workdir/recovered.bin"
```

## CI scenario 2: guarded combine job

Use this only after a security review. A CI system is often the wrong place to reconstruct a production secret because logs, artifacts, caches, and runner access expand the exposure boundary.

If you still need a guarded combine job, use these constraints:

- Trigger it manually.
- Use a hardened or self-hosted runner.
- Keep each required share outside the repository and outside normal logs.
- Gather shares only for the approved session, then restore separated custody. Do not retain `k` shares in one CI secret backend, account, device, administrator domain, or transport channel.
- Mask variables, but do not rely on masking as the only protection.
- Write recovered output to a private file and pass it to one narrow step.
- Delete temporary files before the job exits.

Use the synthetic CI drill above to rehearse commands. Do not provision enough production Recovery shares and their Passphrase as repository secrets: that gives one administrator domain the recovery capability. A reviewed emergency procedure must define temporary collection from independent custodians and cleanup; it cannot use ongoing co-location as its custody model. Actual-backup setup verification belongs in the controlled checkpoint environment, not shared CI.

## CI scenario 3: metadata checks

The current CLI does not expose a metadata-only inspection command. It decodes shares as part of `combine`, but a failed combine with too few shares is not a complete health check for a stored recovery plan.

For routine CI, prefer synthetic round-trip drills. For production metadata checks, write a controlled tool with `safeparts_core` so it can parse packets, inspect set IDs and share indexes, sanitize errors, and avoid logging share text.

## Pipeline hygiene checklist

Before running Safeparts in automation, confirm:

- `set -x` is off.
- The runner does not upload the working directory on failure.
- Temporary files are under a private directory and removed with `trap`.
- Commands write recovered secrets to files, not stdout.
- CI logs do not include command substitutions that expand shares or passphrases.
- Artifacts and caches exclude secret, share, passphrase, and recovered files.
- Alerts and audit events record only the workflow status, not secret material.

## Common failure handling

| Failure | Likely cause | Automation response |
| --- | --- | --- |
| `need at least k shares` | Job supplied too few shares or duplicate shares. | Fail closed. Ask an operator to provide the required count. |
| `could not detect share encoding` | Input is malformed or mixed with non-share text. | Fail closed. Do not print the input. |
| `share set metadata mismatch` | Shares come from different split sets. | Fail closed and restart collection. |
| `duplicate x coordinate` | Same share was supplied twice. | Fail closed and request a different share holder. |
| `passphrase required` | Shares were passphrase-protected but no passphrase file was supplied. | Fail closed and request the passphrase through the approved channel. |
| `decryption failed` | Wrong passphrase or tampered encrypted data. | Fail closed. Do not retry in a tight loop. |

## When to use the Rust library instead

Use `safeparts_core` instead of shell automation when you need:

- metadata inspection without recovery
- custom storage backends
- typed error handling
- in-process passphrase prompts
- stricter control over memory lifetime and logging

See [Rust library integration](rust-library.md).
