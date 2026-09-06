# Retired WinUI application

The native application in [`windows/`](../../../windows/) and its dedicated [`UniFFI bridge`](../../../crates/safeparts_uniffi/) are dormant reference. The application, model, bindings, tests, and package scripts are not supported, built, tested, packaged, or kept in parity with supported surfaces. The bridge is excluded from the root Cargo workspace; old native build and binding-generation commands are not supported.

Retain source, generated reference files, and user-owned local files. No preview promotion or Tauri cutover is pending: both applications are retired. A replacement requires a new decision; there is no plan or timeline. See [`windows/AGENTS.md`](../../../windows/AGENTS.md) and the [bridge contract](../../../crates/safeparts_uniffi/AGENTS.md).

CLI/TUI support on Windows continues. Existing users should keep their recovery shares and passphrase and follow the [supported recovery guidance](../../../README.md#existing-desktop-users). Historical releases remain unchanged.
