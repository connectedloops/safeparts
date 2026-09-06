# Safeparts help docs map

This file tracks the intended coverage, route parity, and page purpose for the help site under `web/help/`.

## Route map

Keep English and Arabic routes aligned.

- `/help/` <-> `/help/ar/`
- `/help/getting-started/` <-> `/help/ar/getting-started/`
- `/help/use-cases/` <-> `/help/ar/use-cases/`
- `/help/security/` <-> `/help/ar/security/`
- `/help/web-ui/` <-> `/help/ar/web-ui/`
- `/help/desktop/` <-> `/help/ar/desktop/`
- `/help/cli/` <-> `/help/ar/cli/`
- `/help/tui/` <-> `/help/ar/tui/`
- `/help/it-devops-guide/` <-> `/help/ar/it-devops-guide/`
- `/help/it-devops-guide/cli-runbooks/` <-> `/help/ar/it-devops-guide/cli-runbooks/`
- `/help/it-devops-guide/break-glass/` <-> `/help/ar/it-devops-guide/break-glass/`
- `/help/it-devops-guide/automation/` <-> `/help/ar/it-devops-guide/automation/`
- `/help/encodings/` <-> `/help/ar/encodings/`
- `/help/project/` <-> `/help/ar/project/`
- `/help/technical-design/` <-> `/help/ar/technical-design/`
- `/help/build-and-run/` <-> `/help/ar/build-and-run/`
- `/help/troubleshooting/` <-> `/help/ar/troubleshooting/`
- `/help/changelog/` <-> `/help/ar/changelog/`
- `/help/developer-guide/` <-> `/help/ar/developer-guide/`
- `/help/developer-guide/library-api/` <-> `/help/ar/developer-guide/library-api/`
- `/help/developer-guide/encodings-and-packets/` <-> `/help/ar/developer-guide/encodings-and-packets/`
- `/help/developer-guide/testing-and-errors/` <-> `/help/ar/developer-guide/testing-and-errors/`

## Sidebar structure

- Start: quickstart, recovery planning, security model.
- Use Safeparts: web app, desktop app, CLI, TUI.
- Operations: operational model, CLI runbooks, break-glass, automation.
- Reference: encodings, how it works, technical design, install/self-host, troubleshooting, changelog.
- Developers: Rust library guide, API, packets, testing.

## Page charters

| Route | Audience | Job | Canonical topics |
| --- | --- | --- | --- |
| `/help/` | New visitors | Orient and route readers to the right path. | Product scope and primary entry points. |
| `/help/getting-started/` | First-time users | Complete a safe practice run. | Minimum k-of-n model and first workflow. |
| `/help/use-cases/` | Planners | Choose a practical recovery plan. | Threshold patterns, holder selection, drills. |
| `/help/security/` | Users and reviewers | Understand risk boundaries. | Threat model, storage rules, passphrases, local execution. |
| `/help/web-ui/` | Browser users | Use the web app safely. | Web-specific workflow and exposed encodings. |
| `/help/desktop/` | Desktop users | Use and build the Tauri app. | Desktop-specific scope and local app commands. |
| `/help/cli/` | CLI users | Run split/combine commands. | CLI syntax, encodings, passphrase file usage. |
| `/help/tui/` | Terminal users | Use the interactive terminal UI. | TUI features, offline workflow, shortcuts. |
| `/help/it-devops-guide/` | Operators | Decide where Safeparts belongs operationally. | Operational fit and minimum runbook questions. |
| `/help/it-devops-guide/cli-runbooks/` | Operators | Write a safe CLI runbook. | File patterns, passphrase files, cleanup. |
| `/help/it-devops-guide/break-glass/` | Incident teams | Design emergency recovery. | Custody, approval, recovery, rotation. |
| `/help/it-devops-guide/automation/` | Platform teams | Run scripts and CI drills safely. | Synthetic drills, CI hygiene, guarded recovery. |
| `/help/encodings/` | All users | Choose a share text format. | Encoding trade-offs and surface availability. |
| `/help/project/` | Curious users | Understand the main mechanics. | Threshold sharing, packets, integrity, passphrases. |
| `/help/technical-design/` | Engineers and reviewers | Review implementation design. | Data flow, packet format, crypto choices, limits. |
| `/help/build-and-run/` | Installers and self-hosters | Install or build Safeparts. | Releases, Rust, web, desktop, Docker, docs. |
| `/help/changelog/` | Users and contributors | Read main-branch and published-release history. | Generated release groups, original commit subjects, and source links; full copy also in root `CHANGELOG.md`. |
| `/help/troubleshooting/` | Users with errors | Diagnose failures safely. | Common errors, WASM setup, build fixes. |
| `/help/developer-guide/` | Rust integrators | Decide whether to use the library. | Library boundary and next pages. |
| `/help/developer-guide/library-api/` | Rust integrators | Call the public API correctly. | Split, combine, encoding, passphrase, API reference. |
| `/help/developer-guide/encodings-and-packets/` | Rust integrators | Store and parse packets. | Packet metadata, text encodings, storage pattern. |
| `/help/developer-guide/testing-and-errors/` | Rust integrators | Test integrations safely. | Synthetic tests, typed errors, logging limits. |

## Non-redundancy rules

- Explain threshold plans with examples first, such as 2 of 3 and 3 of 5. Use k/n notation mainly after that context exists.
- Keep storage and threat-model rules in security. Other pages link there and add only local handling notes.
- Keep encoding trade-offs in encodings. Interface pages list only their exposed formats.
- Keep passphrase behavior in security and technical design. Interface pages explain only how to provide a passphrase.
- Keep CLI command reference in CLI. Operations pages show commands only as part of a procedure.
- Keep cryptographic detail in technical design. Beginner pages stay procedural.
