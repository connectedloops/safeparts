# Retired native macOS application

This SwiftUI application and its dedicated UniFFI bridge are dormant reference. They are not supported, built, tested, packaged, or kept in parity with supported surfaces. The bridge is excluded from the root Cargo workspace, so old native build and binding-generation commands are not supported.

Keep the source, generated reference files, and user-owned local files. A replacement needs a new decision; there is no plan or timeline. Historical releases remain unchanged.

CLI/TUI archives remain available for macOS, Linux, and Windows. Existing users should keep their recovery shares and passphrase and follow the [supported recovery guidance](../README.md#existing-desktop-users). The CLI and TUI support exact binary-file recovery and all Share encodings; the web app supports text workflows.

The retained app keeps operation state in memory, but Swift, Foundation, UniFFI, and the system clipboard may make copies that it cannot fully erase. Other apps may read clipboard contents.

See [AGENTS.md](AGENTS.md) for the reference contract and the [developer notice](../docs/dev/surfaces/macos.md) for scope.
