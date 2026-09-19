# Safeparts desktop UI prototype

This is a throwaway Qt Widgets mock for the issue 140 visual checkpoint. It uses fixed synthetic text and fake demo tokens. It does not call Safeparts core, perform cryptography, parse recovery shares, or protect real secrets.

Run the window from the repository root:

```sh
mise run desktop:prototype
```

Capture the four deterministic UI states and exit:

```sh
mise run desktop:prototype -- --capture apps/desktop/prototype/build/captures
```

The capture command writes `create.png`, `created-shares.png`, `recover-ready.png`, and `recovered-result.png` to the requested directory.

## Local requirements and limits

This prototype was built with Qt 6.9.1 from Homebrew, CMake 4.0.1, and Apple Clang 17 on Apple silicon macOS. It uses the installed Qt and compiler; the selected production desktop runtime may differ. The mock has no production adapter, tests, accessibility review, secure memory handling, persistence, packaging, or platform validation. Completing this visual checkpoint does not complete issue 140.
