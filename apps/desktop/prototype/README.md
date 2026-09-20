# Safeparts desktop UI prototype

This throwaway Qt Widgets mock tests one reference-led direction for the [desktop text round-trip visual checkpoint](https://github.com/connectedloops/safeparts/issues/140). It uses fixed synthetic text and three fake tokens. It does not call Safeparts core, perform cryptography, parse recovery shares, or protect real secrets.

The layout takes its grouping, separators, spacing, and quiet hierarchy from the detail pane in Apple's [Passwords app screenshot](https://support.apple.com/guide/passwords/the-passwords-app-mchl901b1b95/mac). It does not copy the screenshot's sidebar, account content, icons, or window controls. The prototype uses Cocoa window chrome and the system font, plus small Qt-painted or widget-specific treatments for the grouped surfaces, selector, and text fields.

The maintainer [accepted this visual direction and chose to keep Qt](https://github.com/connectedloops/safeparts/issues/140#issuecomment-5747267572). The accepted source is preserved on the local `prototype/desktop-reference-ui` branch at `874c5e7`. Production code must be implemented and tested separately. Visual approval does not establish runtime safety or platform suitability.

Run the window from the repository root:

```sh
mise run desktop:prototype
```

Capture the four deterministic UI states and exit:

```sh
mise run desktop:prototype -- --capture apps/desktop/prototype/build/captures
```

Add `--appearance light` or `--appearance dark` to request a process-local Qt color scheme. Add `--small` to capture the 620 by 480 minimum window. The command writes `create.png`, `created-shares.png`, `recover-ready.png`, and `recovered-result.png` to the requested directory. It also reports the requested and active appearance with the Qt version, platform plugin, widget style, system font, and logical window size.

## Local requirements and limits

This prototype was built with Qt 6.9.1 from Homebrew, CMake 4.0.1, and Apple Clang 17 on Apple silicon macOS. The installed Qt supports process-local light and dark overrides through `QStyleHints`; these overrides do not change the global macOS appearance. Captures use the Qt window ID and contain only this app's synthetic content.

The mock has no production adapter, committed test suite, accessibility review, secure memory handling, persistence, packaging, or broader platform validation. Completing this visual checkpoint does not complete issue 140.
