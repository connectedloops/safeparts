#!/usr/bin/env python3
"""Dormant reference: the former copied web/desktop parity checker.

Not part of supported verification. The retired mirror may diverge indefinitely;
manual execution is historical inspection, not a supported-surface quality gate.
"""

from __future__ import annotations

import difflib
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WEB_SRC = REPO_ROOT / "web" / "src"
DESKTOP_SRC = REPO_ROOT / "desktop" / "src"

STRICT_FILES = [
    "App.tsx",
    "assets/logo.svg",
    "components/ClearButton.tsx",
    "components/CopyButton.tsx",
    "components/KeyboardShortcutsHelp.tsx",
    "components/KeytipsOverlay.tsx",
    "components/LiveRegion.tsx",
    "components/PassphraseInput.tsx",
    "components/PasteButton.tsx",
    "components/SplitForm.tsx",
    "components/ui/encoding-selector.tsx",
    "components/ui/encrypted-text.tsx",
    "components/ui/evervault-card.tsx",
    "context/LiveRegionContext.tsx",
    "hooks/useKeyboardShortcuts.ts",
    "hooks/useLiveRegion.ts",
    "i18n.ts",
    "lib/cn.ts",
    "main.tsx",
    "shims.d.ts",
    "styles.css",
    "vite-env.d.ts",
]

REVIEW_FILES = {
    "components/CombineForm.tsx": "desktop awaits the async Tauri adapter",
    "wasm.ts": "desktop replaces wasm-pack loading with a Tauri command adapter",
}


def read_normalized(path: Path) -> list[str]:
    return [line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()]


def diff(web_rel: str) -> list[str]:
    web_path = WEB_SRC / web_rel
    desktop_path = DESKTOP_SRC / web_rel
    web_lines = read_normalized(web_path)
    desktop_lines = read_normalized(desktop_path)
    return list(
        difflib.unified_diff(
            web_lines,
            desktop_lines,
            fromfile=f"web/src/{web_rel}",
            tofile=f"desktop/src/{web_rel}",
            lineterm="",
        )
    )


def ensure_pair(path: str) -> bool:
    ok = True
    for base in (WEB_SRC, DESKTOP_SRC):
        candidate = base / path
        if not candidate.exists():
            print(f"error: missing parity file {candidate.relative_to(REPO_ROOT)}")
            ok = False
    return ok


def main() -> int:
    failures = 0

    for path in STRICT_FILES:
        if not ensure_pair(path):
            failures += 1
            continue
        delta = diff(path)
        if delta:
            failures += 1
            print(f"error: desktop parity drift in {path}")
            print("\n".join(delta[:80]))
            if len(delta) > 80:
                print("... diff truncated")

    for path, reason in REVIEW_FILES.items():
        if not ensure_pair(path):
            failures += 1
            continue
        delta = diff(path)
        if delta:
            print(f"review: allowed desktop/web difference in {path}: {reason}")
        else:
            print(f"ok: {path} is identical; consider moving it to STRICT_FILES")

    if failures:
        print(f"desktop parity check failed with {failures} issue(s)")
        return 1

    print("desktop parity check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
