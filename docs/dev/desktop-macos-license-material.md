# Local macOS bundle license material

This workflow inventories the exact local bundle built with Qt 6.9.1. It does not certify redistribution compliance.

## Build the material

First build the package. Populate `target/desktop-license-cache/qtbase-6.9.1/LICENSES/` from the `LICENSES` directory at the exact Qt tag [`v6.9.1`](https://github.com/qt/qtbase/tree/v6.9.1/LICENSES), then create `SHA256SUMS` from sorted relative paths. The packaging script pins the cache-manifest hash and works without network access:

```bash
mise run desktop:package:local
mise run desktop:package:licenses
```

Inspect `target/desktop-license-material/inventory.json` and verify `SHA256SUMS`. The output includes the repository MIT text, CXX 1.0.195 MIT and Apache texts, the exact Qtbase 6.9.1 license set, installed Homebrew receipts, Qt/Homebrew SPDX records, the deployed Mach-O-to-component map, and the locked Rust dependency graph.

The inventory deliberately reports `distribution_ready: false`. The current material is still missing exact notices or license texts for several separately bundled Homebrew dylibs, a durably retained Qt 6.9.1 corresponding-source archive, complete build configuration or modification records, and a reviewed distribution replacement procedure.

## Local replacement rehearsal

The ad-hoc local bundle keeps Qt frameworks and plugins dynamically replaceable. A technical rehearsal would copy the bundle, replace only its Qt 6.9.1 ABI-compatible frameworks and Cocoa/macOS-style plugins, run the same load-path normalization and ad-hoc signing steps, then run `desktop:package:smoke` against an identity-bound manifest for that copy. Do not reuse the current manifest after replacement.

No independently built compatible Qt 6.9.1 replacement is available in the approved local material, so that rehearsal is not yet demonstrated. Acquiring or building one is separate from this milestone. Distribution signatures, notarization, and user replacement rights need later review; ad-hoc local signing does not answer those questions.

## Next packaged-workflow seam

The existing `desktop-actions` suite drives public window operations with synthetic data, but it links against the development Qt tree and is not packaged-app evidence. The smallest packaged-binary seam is external macOS UI automation against the relocated `.app`, reusing the synthetic fixtures and authoritative Copy/Save assertions from `desktop-actions`. That route requires Accessibility authorization for the automation process and native-dialog handling. Do not add a hidden production mode or internal command channel merely to avoid that permission. Full packaged workflow automation remains blocked until the owner approves and provisions that host permission or chooses a different external harness.
