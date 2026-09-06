# Safeparts docs style

This maintainer-facing guide keeps the help site clear, current, and non-redundant.

## Voice

- Be direct, calm, and practical.
- Assume recovery may happen under stress.
- Say what Safeparts does and does not do.
- Avoid marketing language, vague guarantees, and filler.
- Prefer short paragraphs and purposeful headings.

## Page quality bar

Each page must answer:

- Who is this for?
- What task or decision does it support?
- What belongs only on this page?
- What should link elsewhere instead of being repeated?
- What should the reader do next?

If a page cannot answer those questions, merge it, cut it, or narrow it.

## Non-redundancy

- One canonical home for each concept. The full root and bilingual changelogs intentionally duplicate generated history; `scripts/dev/changelog.py` owns their content.
- Repeat only the local action, not the whole explanation.
- Use links for background instead of restating previous pages.
- Do not keep a section because an older version had it.

Canonical homes:

- Recovery model: `getting-started.mdx` and deeper detail in `project.mdx`.
- Security rules: `security.mdx`.
- Encodings: `encodings.mdx`.
- CLI syntax: `cli.mdx`.
- Operations process: `it-devops-guide/**`.
- Cryptographic detail: `technical-design.mdx`.
- Rust API: `developer-guide/**`.

## Components

Use components when they improve scanning:

- `<CardGrid>` for page entry points or choice sets.
- `<Steps>` for ordered procedures.
- Callouts only for sharp hazards that deserve interruption, such as break-glass rotation. Prefer normal prose for routine handling guidance.

Avoid decorative components and long procedural bullet lists.

## Threshold language

- Lead with operational wording: "2 of 3", "3 of 5", "required shares", and "total shares".
- Introduce <var>k</var> and <var>n</var> only after the reader has a concrete example.
- Use variables freely in technical design, API, and command references where they match code or flags.
- On client-facing pages, explain what the number choice means for people: who can recover, who can be unavailable, and what gets lost if shares disappear.

## Inline tokens

- Variables: `<var>` for <var>k</var> and <var>n</var> after notation is introduced.
- Keys: `<kbd>`.
- UI and status strings: `<samp>`.
- Small labels: `<span class="token">...</span>`.
- Use `dir="ltr"` in Arabic pages for command names, encodings, equations, and code-like tokens.
- Use inline code only for code, commands, file paths, APIs, and package names.

## Bilingual parity

- Keep a 1:1 file map: `docs/<slug>.mdx` and `docs/ar/<slug>.mdx`; generated changelogs use matching `.md` files.
- Keep section intent aligned. Arabic is not a summary of English.
- Keep navigation locale-relative.
- Preserve RTL quality and add `dir="ltr"` where needed.

## Safety examples

- Use synthetic secrets only.
- Do not include real share packets, real keys, real passphrases, or production-like credentials.
- Do not show workflows that place enough shares and the passphrase in one uncontrolled location without a warning.
