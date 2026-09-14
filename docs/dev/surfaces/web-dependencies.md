# Web dependency review

Review: 2026-09-14 for [#122](https://github.com/connectedloops/safeparts/issues/122), against main `eaeeaac`. Scope: the main web dependency graph, including build, deployment and test tools. This is a dated assessment of known advisories.

## Result and reproduction

Run `mise run security:scan` from the repository root. The shared scan includes every severity and development dependency in the supported Cargo, web and help lockfiles. See [dependency scans](../dependency-scans.md) for freshness checks and report retention.

Trivy 0.74.0 used database schema 2, updated `2026-09-14T07:14:49.440811148Z`, with SHA-256 `20ff733b07b93396b8b256c67cc53baffba85e592e7958263ca49b74beda749e`. The before and after scans used that same current database.

| Main web severity | Before | After |
| --- | ---: | ---: |
| Critical | 0 | 0 |
| High | 16 | 0 |
| Medium | 6 | 0 |
| Low | 2 | 0 |
| Unknown | 0 | 0 |
| Total | 24 | 0 |

Cargo and help each had zero findings before and after; their manifests and lockfiles were unchanged. The main web inventory changed from 1438 to 1401 package entries. Counts are package/advisory occurrences, not unique CVEs. No overrides, suppressions or exceptions were added.

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
| picomatch | 4.0.3 | 4.0.7 | Pattern-handling fixes within parent ranges |
| postcss-selector-parser | 6.1.2 | 6.1.4 | Serialization fix on Tailwind 3's existing parser line |
| Netlify CLI | 27.4.1 | 27.5.2 | Upstream removal of extract-zip and adoption of patched TOML |
| Wrangler | 4.127.1 | 4.131.0 | First release with Miniflare's pinned Sharp 0.35.4 |

Netlify 27.5.2 requires `@netlify/zip-it-and-ship-it` `^15.5.1`, which accepts TOML `^4.2.0`. The resolved `@netlify/dev` 5.1.0 uses `@netlify/functions-dev` 2.0.5, which no longer depends on extract-zip. The registry still publishes extract-zip 2.0.1 without a patched release; removing its upstream dependency path avoids a forced replacement or exception.

The existing Netlify 27.4.1 already required Node >=22.13.0 despite the repository's older pin. The approved remediation synchronizes Node 22.13.0 in `mise.toml` and active web CI. Provider commands use Node shebangs, not Bun's runtime. Bun stays at 1.3.11; Rust and WASM tools keep their pins. Netlify's older Node-compatible major retains vulnerable TOML and is not a safe downgrade.

React, Motion, TypeScript, Tailwind, the React plugin and browser test runner keep their existing locked versions. Vite remains on version 6 with its existing build targets and `/help` proxy. Help keeps its independent dependency graph. The selected transitive records come from Bun resolution within parent ranges; the final manifest contains no added direct transitive dependencies. Use `bun install --frozen-lockfile` to reproduce the reviewed graph.

## Exposure assessment

- Babel and PostCSS can read build-host files through crafted source-map references. PostCSS also has CSS escaping and resource-exhaustion findings. Checked-in inputs reduce exposure but do not make contributions or dependency contents trusted. Generated assets can carry the consequences of a compromised build.
- Browserslist, baseline-browser-mapping, picomatch, nanoid and selector parsing process build queries, patterns or generated data. Crafted inputs can exhaust resources or corrupt processing. They are remediated rather than excluded as development tools.
- Rollup's path-traversal advisory affects file writes during bundling. Vite's WebSocket, source-map and Windows path/editor advisories affect development machines, even though production serves static files. Keep development servers local.
- Netlify's ZIP extraction could write outside the destination through archive symlinks. TOML parsing could execute code or exhaust the stack. The deployment workflow uploads an existing static artifact with `--no-build`, without functions, but the CLI still runs on a credentialed host. The vulnerable dependencies are removed or upgraded rather than treated as unreachable exemptions.
- Wrangler's Miniflare brings Sharp and native image libraries. [The Sharp advisory](https://github.com/advisories/GHSA-rgj7-g3m4-5g8c) calls for 0.35.4 with bundled libheif 1.23.2. Static asset deployment does not use application image uploads; local emulation and tool imports still belong in the scan. Custom global libvips/libheif installations need a separate version check.

## Verification and limits

Use `mise run verify`, the full built-site browser suite and `bun run test:wasm`. Exercise synthetic Split/Combine and English/Arabic app/help navigation with the local browser skill. Provider checks are credential-free: `netlify deploy --help`, artifact preparation and Wrangler's dry run. Do not deploy merely to test a dependency update.

The local checks exercised macOS ARM64 and the Linux ARM64 container smoke. Sharp loaded its bundled libheif 1.23.2 on macOS. The lockfile also includes optional packages for other platforms; those entries are scan coverage, not proof of native execution on Windows or Linux x64. Sharp's optional Windows IA32 binary requires Node `^20.9.0` in both the old 0.35.2 and patched 0.35.4 releases, so it cannot be used with the provider CLI's Node 22 runtime. This existing 32-bit tooling limitation does not change browser support or the supported CLI/TUI release platforms.

Hosted checks remain necessary before deployment. A clean package scan does not audit container images, global native libraries, source code, or retired applications. Preserve the independent `mise run audit` RustSec policy.
