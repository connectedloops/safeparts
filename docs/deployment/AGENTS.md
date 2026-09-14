# AGENTS.md — Deployment guides

## Purpose

Owns operational documentation for supported production deployments.

## Ownership

- `web-artifact.md`: tested Web artifact, Netlify and Cloudflare credentials, and byte-level validation.
- `cloudflare-mismatch-2026-09.md`: dated mismatch observations, evidence links, and unresolved cause; not an operating procedure.
- `docker.md`: unprivileged Docker self-hosting, health checks, and local smoke verification.

## Local Contracts

- Keep existing deployment providers documented as additive unless a change explicitly replaces one.
- Document credential names and required permissions, never credential values.

## Work Guidance

- Keep setup steps short and tied to the checked-in deployment configuration.

## Verification

- Validate the referenced deployment command when the required build artifacts are available.

## Child DOX Index

- No child AGENTS.md files yet.
