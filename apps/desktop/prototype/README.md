# Safeparts desktop UI prototype

This throwaway Qt Widgets mock tests whether Qt can deliver a convincing macOS utility for the [desktop text round-trip visual checkpoint](https://github.com/connectedloops/safeparts/issues/140). It uses fixed synthetic text and fake demo tokens. It does not call Safeparts core, perform cryptography, parse recovery shares, or protect real secrets.

The current design question is whether a restrained native window makes Create, per-share Copy, explicit Recover, readiness feedback, and Start over feel clear to a nontechnical person. The first layout was rejected for its dated appearance. This candidate uses the Cocoa style, system typography, native controls, and a quieter information hierarchy. It still awaits visual judgment. A screenshot cannot settle whether Qt is suitable for the production app.

Run the window from the repository root:

```sh
mise run desktop:prototype
```

Capture the four deterministic UI states and exit:

```sh
mise run desktop:prototype -- --capture apps/desktop/prototype/build/captures
```

The capture command writes `create.png`, `created-shares.png`, `recover-ready.png`, and `recovered-result.png` to the requested directory. It also prints the active Qt version, platform plugin, widget style, and resolved application font.

## Local requirements and limits

This prototype was built with Qt 6.9.1 from Homebrew, CMake 4.0.1, and Apple Clang 17 on Apple silicon macOS. It uses the installed Qt and compiler; the selected production desktop runtime may differ. The mock has no production adapter, committed test suite, accessibility review, secure memory handling, persistence, packaging, or broader platform validation. Completing this visual checkpoint does not complete issue 140.
