# Web artifact deployment

The Web release unit is the commit-identified static artifact produced by [the Web workflow](../../.github/workflows/web-ci.yml). It contains the WASM application and the English and Arabic help sites. Netlify and Cloudflare Workers download and publish this same artifact; neither provider builds the application from source.

Main-branch pushes and manual Web workflow runs on `main` can deploy after the same complete gates pass. Scheduled, pull request, and other verification-only runs use cancelable concurrency groups, but they do not share a cancel-in-progress group with deployment-capable `main` runs. The changelog workflow requests that manual run after committing generated pages, because pushes made with `GITHUB_TOKEN` do not trigger push workflows. Manual runs on other branches and pull requests cannot deploy.

Deployment-capable `main` runs are serialized for each branch instead of canceled. Each provider job also performs a point-in-time stale-artifact check by comparing `github.sha` with the current `origin/main` before it publishes. If a newer `main` commit exists at that check, the provider publish and byte check are skipped for the stale artifact. A newer commit still has to pass the complete Web gate before it is deployed; a failing newer build leaves the last verified deployment in place rather than replacing it with unvalidated output.

## Artifact evidence

The build job runs the WASM boundary tests, typecheck, full browser and accessibility suite, application build, and help build before upload. The retained artifact contains:

- `site/`: the exact provider upload directory
- `evidence/metadata.json`: source commit and reviewed Rust, Bun, Node, `wasm-pack`, and `wasm-bindgen` versions
- `evidence/content-manifest.sha256`: a sorted digest for every Web and help file
- `evidence/artifact-digest.sha256`: the commit and deploy-content digest

GitHub Actions also records the upload archive digest. The provider jobs verify the downloaded directory before publishing. After deployment, they request identity-encoded responses and compare every served file with the retained manifest. Remote mismatch errors print bounded identity diagnostics: expected and observed metadata commit and digest fields, byte counts, SHA-256 digests, response date, URL, and any provider deployment identifier supplied to the verifier. They do not print artifact bodies.

## Cloudflare mismatch diagnosis

On a metadata mismatch, collect the verifier diagnostics and Wrangler deployment/version identifier. Keep byte verification strict; do not add retries without evidence of propagation delay. Use the evidence to decide whether a bounded readiness check or a provider-specific fix is needed.

The [September 2026 incident record](cloudflare-mismatch-2026-09.md) preserves the earlier observations and their limits.

## GitHub configuration

Netlify uses:

- `NETLIFY_AUTH_TOKEN` secret
- `NETLIFY_SITE_ID` secret
- `NETLIFY_SITE_URL` repository variable, such as `https://safeparts.netlify.app`

Cloudflare Workers uses:

- `CLOUDFLARE_API_TOKEN` secret
- `CLOUDFLARE_SITE_URL` repository variable for the configured Worker or custom domain

Use the narrowest deployment permissions each provider supports. Before enabling deployment, have the credential owner verify the selected Netlify site/team or Cloudflare account/Worker and record the granted scope. This guide has not verified provider-specific minimum permissions or whether either token can be limited to a single site; do not assume site-only access. That permission review remains a deployment prerequisite, not a claim about existing credentials.

Start with [Cloudflare token setup](https://developers.cloudflare.com/fundamentals/api/get-started/create-token/) and [Netlify configuration variables](https://docs.netlify.com/build/configure-builds/environment-variables/#netlify-configuration-variables), then verify current provider permissions with the account owner.

 A provider job reports a notice and skips when its credential is absent. The other provider can still deploy. If credentials are present, the matching site URL is required so the post-deploy byte check cannot be skipped.

Netlify Git builds are disabled in [`netlify.toml`](../../netlify.toml). Do not add a build hook or provider build command. [`wrangler.jsonc`](../../wrangler.jsonc) points only at the downloaded `web/dist` asset directory.

## Credential-free local verification

Complete [onboarding](../dev/onboarding.md#2-install-tools), then use Bash from the repository root. Install from the frozen locks, build once, and prepare the same package shape used by CI:

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
  --node-version 22.13.0 \
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
