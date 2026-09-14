# Help dependency review

Review: 2026-09-14 for [#115](https://github.com/connectedloops/safeparts/issues/115). Scope: `web/help/bun.lock`, including development dependencies. This is a dated assessment, not a repository-wide security clearance.

## Historical native-scan result

The counts below preserve the #115 evidence, not a current security clearance. The subsequent [#122 review](web-dependencies.md#result-and-reproduction) identified nested Bun package-identity gaps in Trivy's native lockfile parser. Treat these native-scan counts as limited historical evidence, superseded as a basis for claiming complete Bun coverage. The shared scanner reconciles canonical npm identities through Trivy's SBOM scan; it no longer uses the native Bun parser.

Trivy 0.74.0, vulnerability DB schema 2, updated `2026-09-14T01:15:36Z`, reproduced the pinned `6f586c8` help baseline. That scan reported no vulnerabilities in the refreshed help lockfile:

| Severity | Before | After |
| --- | ---: | ---: |
| Critical | 1 | 0 |
| High | 15 | 0 |
| Medium | 10 | 0 |
| Low | 3 | 0 |
| Unknown | 0 | 0 |
| Total | 29 | 0 |

The historical after-scan inventory contained 507 scanner package entries, including `rollup@4.63.2` marked `Dev: true` and optional platform packages. These entries are not independently reconciled package identities. Lock records, package identities, and package/advisory occurrences are different counts.

## Current scan entry point

Run from the repository root:

```bash
mise run security:scan
```

Read [dependency scans](../dependency-scans.md#supported-scope) for canonical identity reconciliation, scope, database freshness, and artifacts. Keep the normalized inventories, scanner JSON, input hashes, and DB metadata as evidence. The [corrected review](web-dependencies.md#result-and-reproduction) records 507 help lock records mapping to 496 identities, all scanned, with zero help findings. It also records two remaining main-web highs, so the supported scan exits 1, not 0. These are cited results from that review, not a fresh scan for this documentation update. Keep RustSec independent.

## Compatible versions

The npm registry and upstream advisory records were checked before installation. Astro 7.3.2 requires Node >=22.12.0, satisfied by the Node 22.13.0 pin in `mise.toml`. Starlight 0.41.11 accepts Astro `^7.0.2` and the optional Markdown processor `@astrojs/markdown-remark` `^7.2.0`; the selected processor 7.3.1 also satisfies Astro's `^7.3.0` peer.

[Astro's AVIF advisory](https://github.com/advisories/GHSA-26w7-cxv4-gfx2) identifies 7.2.8 as the first patched Astro version. [Sharp's libheif advisory](https://github.com/advisories/GHSA-rgj7-g3m4-5g8c) requires Sharp 0.35.4 with bundled libheif 1.23.2. Install the prebuilt dependencies normally; a custom global libheif/libvips installation needs its own version check.

Starlight stays on the 0.41 line. [Starlight 0.42](https://github.com/withastro/starlight/releases/tag/%40astrojs%2Fstarlight%400.42.0) changes mobile navigation markup and minimum browser support, neither of which is needed for this remediation. [Astro 7](https://github.com/withastro/astro/releases/tag/astro%407.0.0) changes the default Markdown processor and whitespace handling. The help config explicitly retains the remark pipeline and HTML-aware compression to preserve existing content rendering. Astro's Vite 8 dependency stays inside the help package; the main app's Vite dependency is unchanged.

The lockfile was regenerated with the pinned Bun 1.3.11 after upgrading direct dependencies. This refreshes transitive packages within their parents' declared ranges, without overrides or forcing unrelated major versions:

| Package | Before | After | Baseline findings |
| --- | --- | --- | --- |
| Astro | 6.3.7 | 7.3.2 | 1 critical, 1 high, 4 medium, 1 low |
| Sharp | 0.34.5 | 0.35.4 | 2 high |
| Vite | 7.3.3 | 8.3.0 | 1 high, 1 medium |
| PostCSS | 8.5.6 | 8.5.28 | 2 high, 2 medium |
| js-yaml | 4.1.1 | 4.3.2 | 3 high, 1 medium |
| nanoid | 3.3.11 | 3.3.19 | 3 high |
| SVGO | 4.0.1 | 4.1.0 | 2 high, 1 medium |
| smol-toml | 1.6.0 | 1.8.0 | 1 high, 1 medium |
| esbuild | 0.27.7 | 0.28.2 | 1 low |
| postcss-selector-parser | 6.1.2 | 6.1.4 | 1 low |

## Exposure assessment

- **Build inputs:** Astro/Sharp image optimization can process malicious AVIF or other image inputs during a build, even without a deployed server. The site uses checked-in content and assets, not user uploads, but contributions and dependencies still cross the build trust boundary. YAML/TOML parsing can exhaust CPU or stack on crafted frontmatter. PostCSS source-map handling can read build-host files, and selector parsing can exhaust resources. These packages were updated rather than exempted as build-only dependencies.
- **Development:** Vite's Windows file-deny bypass (CVE-2026-53571) and editor UNC-path handling (CVE-2026-53632) affect developer machines when those server paths are reachable. esbuild's Windows `servedir` traversal (GHSA-g7r4-m6w7-qqqr) requires its own serving API, which the help scripts do not invoke. Keep development servers local; static deployment does not protect development hosts. The explicit dev dependency Rollup and its platform packages were included in the scan and refreshed.
- **Generated output:** Astro attribute/transition escaping, PostCSS style escaping, and SVGO's incomplete `removeScripts` filtering can leave executable content in generated HTML, CSS, or SVG when supplied attacker-controlled input. The help config does not accept request-derived props or configure SVGO as an upload sanitizer. Reviewed source inputs reduce exposure, but static hosting can still serve poisoned build output. Do not treat an optimizer as a sanitizer. nanoid's invalid-size flaws concern build-tool ID generation here, not Recovery shares, whose generation remains in Rust/WASM.
- **Server-only paths:** Astro's prerendered error-page Host-header SSRF (CVE-2026-54299) and base-path middleware authorization bypass (CVE-2026-84376) require runtime Astro request handling. `astro.config.mjs` has no server adapter, middleware authorization, or on-demand routes; the build emits static files to `web/dist/help/`. Production serves that artifact rather than an Astro process. This limits those paths specifically, not all 29 findings.

## Historical follow-up and scope

The #115 scan reported zero help findings and 24 main-web findings (0 critical, 16 high, 6 medium, 2 low, 0 unknown), all marked `Dev: true`. Omitting development dependencies gave zero for the main-web target. That comparison showed the effect of the inclusion flag in that scanner output; it did not establish correct nested Bun identities or complete coverage. No suppression file or exception policy was added.

The broad historical scan also found dormant desktop/mobile and installed example dependencies. The supported scan uses only its named locked graphs: it stages Cargo alone and scans normalized Bun inventories; retired inventory is separate. Neither the historical help result nor a clean help graph establishes that the full supported scan passes. The [main-web review](web-dependencies.md#unresolved-upstream-constraint) retains the two unresolved Sharp highs without an approved exception.

## Verification

Use frozen installs with the pinned toolchain, then the combined build and existing tests described in [Help docs](help-docs.md#useful-checks) and [Verification](../verification.md). For dependency upgrades, retain before/after scan evidence alongside build results. Browser smoke must cover English/Arabic navigation, search assets, and the help-to-app round trip against the complete static artifact.
