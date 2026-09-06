#!/usr/bin/env python3
"""Generate committed changelogs from complete main history and explicit release JSON."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import quote

OUTPUTS = ('CHANGELOG.md', 'web/help/src/content/docs/changelog.md',
           'web/help/src/content/docs/ar/changelog.md')
BOT_SUBJECT = 'chore(changelog): update generated history'
BASE = 'https://github.com/connectedloops/safeparts'
CATEGORIES = {
    'feat': ('Features', 'ميزات'), 'fix': ('Fixes', 'إصلاحات'),
    'perf': ('Performance', 'الأداء'), 'docs': ('Documentation', 'التوثيق'),
    'refactor': ('Refactoring', 'إعادة هيكلة'), 'test': ('Tests', 'اختبارات'),
    'build': ('Build', 'البناء'), 'ci': ('Automation', 'الأتمتة'),
    'chore': ('Maintenance', 'الصيانة'), 'style': ('Code style', 'تنسيق الشيفرة'),
    'revert': ('Reverts', 'تراجعات'), 'other': ('Other changes', 'تغييرات أخرى'),
}


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(['git', '--no-replace-objects', '-C', str(repo), *args],
                            text=True, capture_output=True)
    if result.returncode:
        raise ValueError(f'Git failed ({args[0]}): fetch complete main history and tags first.')
    return result.stdout.rstrip('\n')


def collect(repo: Path, main_ref: str, metadata: object) -> tuple[list, list]:
    """Collect ordered commits and eligible releases; never infer main from HEAD."""
    refs = {'main': 'refs/heads/main', 'origin/main': 'refs/remotes/origin/main'}
    if main_ref not in refs:
        raise ValueError('--main-ref must be main or origin/main, never a feature HEAD.')
    if git(repo, 'rev-parse', '--is-shallow-repository') != 'false':
        raise ValueError('Shallow history: fetch --unshallow and all tags before generating.')
    grafts = Path(git(repo, 'rev-parse', '--git-path', 'info/grafts'))
    if not grafts.is_absolute():
        grafts = repo / grafts
    if grafts.exists() and grafts.read_text().strip():
        raise ValueError('Grafted history is incomplete; remove grafts before generating.')
    tip = git(repo, 'rev-parse', '--verify', refs[main_ref] + '^{commit}')
    rows = git(repo, 'log', '--topo-order', '--format=%H%x09%s', tip).splitlines()
    reachable = {row.split('\t', 1)[0] for row in rows}
    commits = []
    for row in rows:
        sha, subject = row.split('\t', 1)
        if subject == BOT_SUBJECT:
            # Compare every parent separately: merges carrying real work must remain.
            parents = git(repo, 'rev-list', '--parents', '-n', '1', sha).split()[1:]
            changed = set()
            for parent in parents or [None]:
                args = ('diff', '--name-only', '-z', parent, sha) if parent else (
                    'diff-tree', '--root', '--no-commit-id', '--name-only', '-r', '-z', sha)
                changed.update(filter(None, git(repo, *args).split('\0')))
            if changed and changed <= set(OUTPUTS):
                continue
        commits.append((sha, subject))
    if not isinstance(metadata, list):
        raise ValueError('Release JSON must be an array or gh --paginate --slurp page arrays.')
    releases = []
    seen = set()
    for page in metadata:
        for release in page if isinstance(page, list) else [page]:
            if not isinstance(release, dict):
                raise ValueError('Each release must be an object.')
            if release.get('draft') or not release.get('published_at'):
                continue
            tag = release.get('tag_name')
            if not isinstance(tag, str) or not tag:
                raise ValueError('Published release is missing tag_name.')
            if tag in seen:
                raise ValueError(f'Duplicate release tag: {tag!r}')
            seen.add(tag)
            git(repo, 'check-ref-format', 'refs/tags/' + tag)
            sha = git(repo, 'rev-parse', '--verify', 'refs/tags/' + tag + '^{commit}')
            if sha not in reachable:
                continue
            published = datetime.fromisoformat(release['published_at'].replace('Z', '+00:00'))
            if published.tzinfo is None:
                raise ValueError('Release published_at must include a timezone.')
            releases.append(dict(tag=tag, name=release.get('name') or tag,
                                 published=published.astimezone(timezone.utc),
                                 prerelease=bool(release.get('prerelease')),
                                 reachable=set(git(repo, 'rev-list', sha).splitlines())))
    releases.sort(key=lambda r: (r['published'], r['tag']))
    return commits, releases


def escape(text: str) -> str:
    """Entities keep untrusted punctuation literal in both Markdown and raw HTML."""
    return ''.join(c if c.isalnum() or c == ' ' else f'&#{ord(c)};' for c in text)


def render(commits: list, releases: list, arabic: bool = False) -> str:
    """Assign each commit to the earliest published release that contains it."""
    groups = []
    assigned = set()
    for release in releases:
        entries = [(sha, subject) for sha, subject in commits
                   if sha in release['reachable'] and sha not in assigned]
        assigned.update(sha for sha, _ in entries)
        groups.append((release, entries))
    groups.append((None, [(sha, subject) for sha, subject in commits if sha not in assigned]))
    intro = ('سجل كامل للتغييرات في الفرع الرئيسي والإصدارات المنشورة. عناوين التغييرات بلغتها الأصلية.'
             if arabic else 'Complete main-branch history and published releases. Commit subjects retain their original wording.')
    lines = ['<!-- Generated by scripts/dev/changelog.py. Do not edit. -->', '', intro, '']
    for release, entries in reversed(groups):
        if release is None:
            heading = 'غير مُصدر' if arabic else 'Unreleased'
        else:
            title = escape(release['name'])
            if release['name'] != release['tag']:
                title += ' (' + escape(release['tag']) + ')'
            if arabic:
                title = '<span dir="ltr">' + title + '</span>'
            heading = f'[{title}]({BASE}/releases/tag/{quote(release["tag"], safe="")})'
            heading += ' (' + release['published'].date().isoformat() + ')'
            if release['prerelease']:
                heading += ' · ' + ('إصدار تجريبي' if arabic else 'Prerelease')
        lines.extend(['## ' + heading, ''])
        if not entries:
            lines.extend(['لا توجد تغييرات إضافية.' if arabic else 'No additional commits.', ''])
        categorized = {key: [] for key in CATEGORIES}
        for sha, subject in entries:
            match = re.match(r'^([a-z]+)(?:\([^\r\n)]+\))?!?: .+', subject)
            key = match[1] if match and match[1] in CATEGORIES else 'other'
            text = escape(subject)
            link = f'[{sha[:7]}]({BASE}/commit/{sha})'
            entry = text + ' (' + link + ')'
            if arabic:
                entry = '<span dir="ltr">' + entry + '</span>'
            categorized[key].append('- ' + entry)
        for key, titles in CATEGORIES.items():
            if categorized[key]:
                lines.extend(['### ' + titles[int(arabic)], '', *categorized[key], ''])
    return '\n'.join(lines)


def generate(repo: Path, main_ref: str, metadata: object) -> dict[str, str]:
    commits, releases = collect(repo, main_ref, metadata)
    english = render(commits, releases)
    arabic = render(commits, releases, True)
    return dict(zip(OUTPUTS, ('# Changelog\n\n' + english,
                             '---\ntitle: Changelog\ndescription: Main history and published releases.\n---\n\n' + english,
                             '---\ntitle: سجل التغييرات\ndescription: تاريخ الفرع الرئيسي والإصدارات المنشورة.\n---\n\n' + arabic)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path('.'))
    parser.add_argument('--main-ref', choices=('main', 'origin/main'), required=True)
    parser.add_argument('--releases-json', type=Path, required=True)
    args = parser.parse_args()
    try:
        outputs = generate(args.repo, args.main_ref, json.loads(args.releases_json.read_text()))
        for name, content in outputs.items():
            target = args.repo / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding='utf-8')
    except (ValueError, OSError, TypeError, KeyError) as error:
        print(f'changelog: {error}', file=sys.stderr)
        return 1
    print('Generated ' + ', '.join(OUTPUTS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
