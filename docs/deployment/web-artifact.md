# Web artifact deployment

The Web release unit is the commit-identified static artifact produced by [the Web workflow](../../.github/workflows/web-ci.yml). It contains the WASM application and the English and Arabic help sites. Netlify and Cloudflare Workers download and publish this same artifact; neither provider builds the application from source.

Main-branch pushes and manual Web workflow runs on `main` can deploy after the same complete gates pass. The changelog workflow requests that manual run after committing generated pages, because pushes made with `GITHUB_TOKEN` do not trigger push workflows. Manual runs on other branches and pull requests cannot deploy.

## Artifact evidence

The build job runs the WASM boundary tests, typecheck, full browser and accessibility suite, application build, and help build before upload. The retained artifact contains:

- `site/`: the exact provider upload directory
- `evidence/metadata.json`: source commit and reviewed Rust, Bun, Node, `wasm-pack`, and `wasm-bindgen` versions
- `evidence/content-manifest.sha256`: a sorted digest for every Web and help file
- `evidence/artifact-digest.sha256`: the commit and deploy-content digest

GitHub Actions also records the upload archive digest. The provider jobs verify the downloaded directory before publishing. After deployment, they request identity-encoded responses and compare every served file with the retained manifest.

## GitHub configuration

Netlify uses:

- `NETLIFY_AUTH_TOKEN` secret
- `NETLIFY_SITE_ID` secret
- `NETLIFY_SITE_URL` repository variable, such as `https://safeparts.netlify.app`

Cloudflare Workers uses:

- `CLOUDFLARE_API_TOKEN` secret
- `CLOUDFLARE_SITE_URL` repository variable for the configured Worker or custom domain

Use provider credentials that can deploy only the intended site. A provider job reports a notice and skips when its credential is absent. The other provider can still deploy. If credentials are present, the matching site URL is required so the post-deploy byte check cannot be skipped.

Netlify Git builds are disabled in [`netlify.toml`](../../netlify.toml). Do not add a build hook or provider build command. [`wrangler.jsonc`](../../wrangler.jsonc) points only at the downloaded `web/dist` asset directory.

## Credential-free local verification

Install from the frozen locks, build once, and prepare the same package shape used by CI:

```bash
mise install
bun install --cwd web --frozen-lockfile
bun install --cwd web/help --frozen-lockfile
bun run --cwd web build:wasm
bun run --cwd web typecheck
bun run --cwd web build
bun run --cwd web/help build
rm -rf target/web-deploy
python3 web/scripts/deploy-artifact.py prepare \
  --site web/dist \
  --evidence target/web-deploy/evidence \
  --source-commit "$(git rev-parse HEAD)" \
  --rust-version 1.93.0 \
  --bun-version 1.3.11 \
  --node-version 22.12.0 \
  --wasm-pack-version 0.15.0 \
  --wasm-bindgen-version 0.2.108
mkdir -p target/web-deploy/site
cp -a web/dist/. target/web-deploy/site/
python3 web/scripts/deploy-artifact.py verify \
  --site target/web-deploy/site \
  --evidence target/web-deploy/evidence
WRANGLER_SEND_METRICS=false web/node_modules/.bin/wrangler deploy \
  --dry-run --config wrangler.jsonc
```

This validates the deployment package without credentials and does not publish it. To check an already served artifact, run:

```bash
python3 web/scripts/deploy-artifact.py verify-remote \
  --base-url https://example.invalid \
  --evidence target/web-deploy/evidence
```

Do not deploy from a local source rebuild. Production deployment is owned by the `main` branch provider jobs after the complete Web gate passes.
