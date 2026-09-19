# Throwaway Qt memory-only printing probe

Decision: [Validate memory-only Qt printing on Linux](https://github.com/connectedloops/safeparts/issues/139). This is a synthetic-data technical probe, not a desktop app or a production print module.

## Question and result

Can Qt render a complete PDF in memory and send it to a Linux CUPS print destination without creating an application-side sensitive temporary file?

**Yes, in the tested headless Ubuntu container.** `QPdfWriter` → `QBuffer` → streamed CUPS submission delivered an identical PDF to a simulated print backend. No application-side file opens for writing were observed in the memory, cancellation, or missing-printer scenarios. The ordinary `QPrinter` control did create temporary files and write PDF bytes to one before deleting it.

This establishes the narrow mechanism only. **Ubuntu desktop, Arch/Hyprland, portal/dialog integration, physical printers, crash behavior, and maximum-size performance remain unverified.** No Rust interface or Safeparts cryptography is included.

## Run

From this throwaway worktree:

```sh
bash docs/agents/qt-memory-printing-prototype.sh
```

Docker must already be running. The user approved dependency downloads inside disposable Linux containers. The build installs Qt/CUPS and inspection tools only in the image; the run uses `--network none` and `SYS_PTRACE` to trace its own synthetic probe. It mounts only a freshly created evidence directory and removes the container afterward. The local image `safeparts-qt-memory-print-prototype:local` and evidence directory remain available for inspection. No host printer configuration is changed.

**Never supply real secrets or recovery shares.** The tracer records syscall payloads, and the simulated OS print destination deliberately retains PDFs for comparison. Execute `.container.sh` only inside the dedicated container: it changes that container's CUPS configuration.

## Evidence

Captured 2026-09-19:

- Ubuntu 24.04 arm64 userspace on Docker Desktop's LinuxKit kernel.
- Qt 6.4.2 (`6.4.2+dfsg-21.1build5`), CUPS/libcups 2.4.7 (`2.4.7-1.2ubuntu7.14`), Poppler 24.02.0, strace 6.8.
- Qt `offscreen` platform; private Unix-domain CUPS socket; simulated raw backend, no physical printer.
- Both successful paths produced three pages with all 120 synthetic text rows. The streamed PDF's SHA-256 matched the backend's received PDF exactly.
- The cancellation path aborted a half-uploaded held job; CUPS reported the job cancelled, and the backend received no output for it. The nonexistent-printer path failed without a printed result.
- Memory/cancel/failure traces contained no write-capable file opens and no IPv4/IPv6 connection attempts. Regular-file writes were diagnostic stdout/stderr; runtime pipe wakeups were also observed. PDF transfer used the CUPS Unix socket.
- The control trace captured temporary-file creation, PDF writes and deletion, plus attempted DNS connections despite the isolated network. Disconnected execution alone is not evidence of absent network attempts.

The [evidence archive](qt-memory-printing-prototype.evidence.tar.gz) contains the source hashes, image ID, environment, summaries, raw syscall traces, synthetic backend PDFs/text extraction, and scenario logs. SHA-256: `a3d8948173f3a7dc55149a63b210413f8d98a386e3f8afa409c83cda4f61ac99`.

The summary's regex extraction is a navigation aid, not a security proof. The parent also inspected actual regular-file writes and the control PDF writes. Whole-application storage/network behavior needs separate validation.

## Limits that matter

- The Ubuntu packages exercise Qt 6.4.2, not the Qt 6.11 source inspected during architecture research. Revalidate against the version ultimately selected.
- A raw CUPS queue is a deliberately simple capture fixture and is deprecated upstream. Do not ship it as the product's printer configuration; production needs supported destination capability/format negotiation.
- This probe avoids printer-selection dialogs and uses a fixed local destination. Adding those facilities can change disk/network behavior.
- Holding jobs until upload completes prevents the probe's cancelled partial document from printing. The minimal code is not production-grade cleanup, retry, timeout or cancellation handling.
- The Docker tag/package repositories are not fully locked. The archive records the exact successful image/packages; future runs may fetch different versions.
- Only the OS/backend fixture writes the captured PDFs. Those intentional output artifacts are not evidence that application-side staging files are acceptable.

No real app implementation was read, modified, or reinstated. Keep these files on `prototype/qt-memory-printing`; they are decision evidence, not a production dependency.
