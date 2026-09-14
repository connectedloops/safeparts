# Web dependency review

Review: 2026-09-14 for [#122](https://github.com/connectedloops/safeparts/issues/122), against main `eaeeaac`. Scope: the main web dependency graph, including build, deployment and test tools. This is a dated assessment of known advisories.

## Result and reproduction

Run `mise run security:scan` from the repository root. The shared scan includes every severity and development dependency in the supported Cargo, web and help lockfiles. See [dependency scans](../dependency-scans.md) for freshness checks and report retention.

Trivy 0.74.0 used database schema 2, updated `2026-09-14T07:14:49.440811148Z`, with SHA-256 `20ff733b07b93396b8b256c67cc53baffba85e592e7958263ca49b74beda749e`. All three corrected scans used that same current database. Remediation is incomplete: two high-severity Sharp findings remain. The supported scan exits 1, not 0; #122 and the dependent #118 gate remain blocked. No security exception was approved.

| Main web severity | Main `eaeeaac` | Rejected `079a76c` | Current graph |
| --- | ---: | ---: | ---: |
| Critical | 0 | 0 | 0 |
| High | 19 | 9 | 2 |
| Medium | 7 | 4 | 0 |
| Low | 2 | 0 | 0 |
| Unknown | 0 | 0 | 0 |
| Total | 28 | 13 | 2 |

The earlier native-Bun scans reported 24 then zero findings. Those results and the original clean handoff are superseded: Trivy misidentified nested resolution keys, leaving reachable vulnerable versions outside the correct advisory lookup. The shared scan now reconciles every canonical npm identity against the actual SBOM scan inventory; see [dependency scans](../dependency-scans.md).

Main web has 1,371 lock records mapping to 1,181 distinct identities, all scanned (main: 1,438/1,219; rejected commit: 1,401/1,187). Help has 507 records mapping to 496 identities, all scanned. Cargo's inventory has 310 packages. Cargo and help have zero findings in all three corrected scans; their manifests and lockfiles are unchanged. Counts are name/version/advisory occurrences per graph, with duplicate source keys preserved, not unique CVEs or lock-record counts. No overrides, suppressions or exceptions were added.

## Unresolved upstream constraint

`netlify-cli@27.5.2` depends on `@netlify/images@2.0.1`, which requires `ipx@^3.1.1`. The latest compatible ipx, 3.1.1, requires `sharp@^0.34.3`; its latest compatible Sharp is 0.34.5. That reachable copy has two high findings:

- [GHSA-f88m-g3jw-g9cj](https://github.com/advisories/GHSA-f88m-g3jw-g9cj), fixed in Sharp 0.35.0.
- [GHSA-rgj7-g3m4-5g8c](https://github.com/advisories/GHSA-rgj7-g3m4-5g8c), fixed in Sharp 0.35.4.

The patched Sharp 0.35.4 used by Wrangler does not replace ipx's nested copy. At review time, Netlify 27.5.2 and images 2.0.1 are their latest releases; only the incompatible ipx 4 beta line accepts Sharp 0.35. A provider change, override or prerelease migration is outside this remediation. Keep these findings visible and #122 open until a compatible upstream release is available or a separate decision authorizes another path.

## Compatible versions

The npm registry manifests and advisory fix ranges informed these selections:

| Package or path | Before | After | Reason |
| --- | --- | --- | --- |
| Vite | 6.4.1 | 6.4.3 | Backported dev-server and editor fixes without a Vite major upgrade |
| PostCSS | 8.5.6 | 8.5.28 | CSS and source-map handling fixes |
| nanoid through PostCSS | 3.3.11 | 3.3.19 | Patched legacy line within PostCSS's range |
| Babel core | 7.28.6 | 7.29.7 | Source-map file-read fix; related Babel 7 packages stay compatible |
| Browserslist | 4.28.1 | 4.28.9 | Prototype pollution and resource-exhaustion fixes |
| baseline-browser-mapping | 2.9.18 | 2.11.23 | Input-handling fix within Browserslist's range |
| Rollup | 4.56.0 | 4.63.2 | Path-traversal fix within Vite 6's range |
| picomatch | 2.3.1 and 4.0.3 | 2.3.2 and 4.0.7 | Pattern-handling fixes within every parent range |
| postcss-selector-parser | 6.1.2 | 6.1.4 | Serialization fix on Tailwind 3's existing parser line |
| Netlify CLI | 27.4.1 | 27.5.2 | Upstream removal of extract-zip and adoption of patched TOML |
| Wrangler | 4.127.1 | 4.131.0 | First release with Miniflare's pinned Sharp 0.35.4 |

Netlify 27.5.2 requires `@netlify/zip-it-and-ship-it` `^15.5.1`, which accepts TOML `^4.2.0`. The resolved `@netlify/dev` 5.1.0 uses `@netlify/functions-dev` 2.0.5, which no longer depends on extract-zip. The registry still publishes extract-zip 2.0.1 without a patched release; removing its upstream dependency path avoids a forced replacement or exception.

The existing Netlify 27.4.1 already required Node >=22.13.0 despite the repository's older pin. The approved remediation synchronizes Node 22.13.0 in `mise.toml` and active web CI. Provider commands use Node shebangs, not Bun's runtime. Bun stays at 1.3.11; Rust and WASM tools keep their pins. Netlify's older Node-compatible major retains vulnerable TOML and is not a safe downgrade.

React, Motion, TypeScript, Tailwind, the React plugin and browser test runner keep their existing locked versions. Vite remains on version 6 with its existing build targets and `/help` proxy. Help keeps its independent dependency graph. Bun regenerated the complete graph with temporary exact constraints on the existing direct versions, then regenerated again against the unchanged real manifest. No package records were spliced between graphs and no scratch constraints remain. This refresh replaces 52 old identities with 46 new identities relative to `079a76c`, including compatible Motion transitive packages, rather than claiming only the named vulnerable copies changed. All 2,182 locked dependency edges satisfy their ranges, and all 1,181 identities match registry names, versions, integrity and recorded dependency ranges. Use `bun install --frozen-lockfile` to reproduce the reviewed graph.

## Exposure assessment

- Babel and PostCSS can read build-host files through crafted source-map references. PostCSS also has CSS escaping and resource-exhaustion findings. Checked-in inputs reduce exposure but do not make contributions or dependency contents trusted. Generated assets can carry the consequences of a compromised build.
- Browserslist, baseline-browser-mapping, picomatch, nanoid and selector parsing process build queries, patterns or generated data. Crafted inputs can exhaust resources or corrupt processing. They are remediated rather than excluded as development tools.
- Rollup's path-traversal advisory affects file writes during bundling. Vite's WebSocket, source-map and Windows path/editor advisories affect development machines, even though production serves static files. Keep development servers local.
- Netlify's ZIP extraction could write outside the destination through archive symlinks. TOML parsing could execute code or exhaust the stack. The deployment workflow uploads an existing static artifact with `--no-build`, without functions, but the CLI still runs on a credentialed host. ZIP and TOML findings are removed or upgraded rather than treated as unreachable exemptions. Netlify's separate image-processing path still brings the two Sharp highs listed above.
- Wrangler's Miniflare brings Sharp and native image libraries. [The Sharp advisory](https://github.com/advisories/GHSA-rgj7-g3m4-5g8c) calls for 0.35.4 with bundled libheif 1.23.2. Static asset deployment does not use application image uploads; local emulation and tool imports still belong in the scan. Custom global libvips/libheif installations need a separate version check.

## Verification and limits

Use `mise run verify`, the full built-site browser suite and `bun run test:wasm`. Exercise synthetic Split/Combine and English/Arabic app/help navigation with the local browser skill. Provider checks are credential-free: `netlify deploy --help`, artifact preparation and Wrangler's dry run. Do not deploy merely to test a dependency update.

The local checks exercised macOS ARM64 and the Linux ARM64 container smoke. Wrangler's Sharp loaded bundled libheif 1.23.2 on macOS; ipx still loads its vulnerable Sharp 0.34.5. A separate installed-tree audit reconciled 1,043 identities with Trivy and found exactly those two highs. It also included `napi-wasm@1.1.0`, bundled inside `@parcel/watcher-wasm` but absent from the lock; that bundled identity had no findings. The shared lock-only scan does not inspect tarball-bundled dependencies. The lockfile also includes optional packages for other platforms; those entries are scan coverage, not proof of native execution on Windows or Linux x64. Sharp's optional Windows IA32 binary requires Node `^20.9.0` in both the old 0.35.2 and patched 0.35.4 releases, so it cannot be used with the provider CLI's Node 22 runtime. This existing 32-bit tooling limitation does not change browser support or the supported CLI/TUI release platforms.

Hosted checks remain necessary before deployment. A clean package scan does not audit container images, global native libraries, source code, or retired applications. Preserve the independent `mise run audit` RustSec policy.
