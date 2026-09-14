# Cloudflare metadata mismatches, September 2026

Historical assessment recorded with [diagnostic change 45654ed](https://github.com/connectedloops/safeparts/commit/45654edefb1012398e91ab62a4bab07a22e2d8b6) for [#111](https://github.com/connectedloops/safeparts/issues/111). This is not a current deployment procedure or a confirmed root cause.

## Evidence

- [2026-09-09 Cloudflare job](https://github.com/connectedloops/safeparts/actions/runs/34332314933/job/102404893131): failed; the same run's build/package, container smoke, and Netlify jobs passed.
- [2026-09-10 Cloudflare job](https://github.com/connectedloops/safeparts/actions/runs/34458188944/job/102810845625): failed with the same surrounding jobs passing.

The original assessment recorded that both jobs uploaded only changed static assets, including `/safeparts-build/metadata.json`, then checked the public Worker URL less than one second after Wrangler reported that triggers were deployed. The verifier failed at the first metadata byte comparison. Local artifact verification had already passed.

The logs available to that assessment lacked the served metadata identity, response date, and Cloudflare version identifier. They could not distinguish stale publication from an incorrect or transformed asset. The diagnostic change added bounded identity evidence; it did not establish propagation delay or justify retries.

For ongoing operation, follow [Cloudflare mismatch diagnosis](web-artifact.md#cloudflare-mismatch-diagnosis).
