# Retired SwiftUI application

The native application in [`macos/`](../../../macos/) and its dedicated [`UniFFI bridge`](../../../crates/safeparts_uniffi/) are dormant reference. They are not supported, built, tested, packaged, or kept in parity with supported surfaces. The bridge is excluded from the root Cargo workspace; old native build and binding-generation commands are not supported.

Retain source, generated reference files, and user-owned local files. There is no replacement plan or timeline; a replacement requires a new decision. See [`macos/AGENTS.md`](../../../macos/AGENTS.md) and the [bridge contract](../../../crates/safeparts_uniffi/AGENTS.md).

CLI/TUI support on macOS continues. Existing users should keep their recovery shares and passphrase and follow the [supported recovery guidance](../../../README.md#existing-desktop-users). Historical releases remain unchanged.
