#!/usr/bin/env python3
"""Isolated history fixtures for the changelog generator."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).with_name('changelog.py')
OUTPUTS = ('CHANGELOG.md', 'web/help/src/content/docs/changelog.md',
           'web/help/src/content/docs/ar/changelog.md')
BOT = 'chore(changelog): update generated history'


class ChangelogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git('init', '-b', 'main')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], text=True).strip()

    def commit(self, subject, path='code.txt'):
        file = self.repo / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(subject + '\n' + (file.read_text() if file.exists() else ''))
        self.git('add', path)
        self.git('commit', '-m', subject)
        return self.git('rev-parse', 'HEAD')

    def release(self, tag, date='2025-01-01T00:00:00Z', **extra):
        return dict(tag_name=tag, name=tag, published_at=date, draft=False,
                    prerelease=False, **extra)

    def generate(self, releases, ref='main', expected=0):
        source = self.repo / 'releases.json'
        source.write_text(json.dumps(releases))
        result = subprocess.run([sys.executable, str(TOOL), '--repo', str(self.repo),
                                 '--main-ref', ref, '--releases-json', str(source)],
                                text=True, capture_output=True)
        self.assertEqual(expected, result.returncode, result.stderr)
        if expected:
            return result.stderr
        return {name: (self.repo / name).read_text() for name in OUTPUTS}

    def test_main_only_including_merges_and_published_annotated_release(self):
        root = self.commit('Initial import')
        self.git('tag', '-a', 'v1', '-m', 'annotated')
        self.git('tag', 'help-snapshot')
        self.git('checkout', '-b', 'merged')
        merged = self.commit('feat: merged work', 'merged.txt')
        self.git('checkout', 'main')
        self.commit('fix: main work')
        self.git('merge', '--no-ff', 'merged', '-m', 'Merge merged')
        self.git('checkout', '-b', 'unmerged', root)
        side = self.commit('feat: hidden side work')
        self.git('tag', 'v99')
        releases = [[self.release('v99')], [self.release('v1')]]
        outputs = self.generate(releases)
        for text in outputs.values():
            self.assertIn(root, text)
            self.assertIn(merged, text)
            self.assertNotIn(side, text)
            self.assertNotIn('help-snapshot', text)
            self.assertIn('2025-01-01', text)
            self.assertEqual(1, text.count('/commit/' + root))
        self.assertIn('## Unreleased', outputs['CHANGELOG.md'])
        self.assertIn('### Features', outputs['CHANGELOG.md'])
        self.assertIn('### Other changes', outputs['CHANGELOG.md'])

    def test_dates_same_commit_prereleases_and_divergent_ancestry(self):
        root = self.commit('Initial import')
        self.git('tag', 'v1')
        self.git('checkout', '-b', 'side')
        side = self.commit('feat: side', 'side.txt')
        self.git('tag', 'v-side')
        self.git('checkout', 'main')
        main = self.commit('fix: main')
        self.git('tag', 'v2')
        self.git('tag', 'v2-preview')
        self.git('merge', '--no-ff', 'side', '-m', 'Merge side')
        tip = self.git('rev-parse', 'HEAD')
        preview = self.release('v2-preview', '2025-02-01T00:00:00Z')
        preview['prerelease'] = True
        releases = [self.release('v2', '2025-03-01T00:00:00Z'), preview,
                    self.release('v-side', '2025-04-01T00:00:00Z'), self.release('v1')]
        text = self.generate(releases)['CHANGELOG.md']
        self.assertLess(text.index('## Unreleased'), text.index('## [v&#45;side]'))
        self.assertLess(text.index('## [v2]'), text.index('## [v2&#45;preview]'))
        self.assertIn('Prerelease', text)
        self.assertIn('## [v2](https://github.com/connectedloops/safeparts/releases/tag/v2) (2025-03-01)\n\nNo additional commits.', text)
        for sha in (root, side, main, tip):
            self.assertEqual(1, text.count('/commit/' + sha))
        self.assertEqual(self.generate(releases), self.generate(list(reversed(releases))))

    def test_no_releases_bot_exclusion_is_narrow_and_idempotent(self):
        original = self.commit('feat: first')
        before = self.generate([])
        self.git('add', *OUTPUTS)
        self.git('commit', '-m', BOT)
        bot = self.git('rev-parse', 'HEAD')
        after = self.generate([])
        self.assertEqual(before, after)
        misleading = self.commit(BOT)
        output = self.generate([])['CHANGELOG.md']
        self.assertIn(original, output)
        self.assertNotIn(bot, output)
        self.assertIn(misleading, output)
        ordinary = self.commit('docs: manual history correction', OUTPUTS[0])
        self.assertIn(ordinary, self.generate([])['CHANGELOG.md'])

    def test_escape_subjects_and_release_titles_with_output_parity(self):
        subject = 'fix(ui)!: <script>alert(1)</script> {x} ``` [link](javascript:bad) & _* \\ test'
        sha = self.commit(subject)
        self.git('tag', 'v1')
        release = self.release('v1')
        release['name'] = '<img src=x onerror=alert(1)> {danger} ``` & [bad]'
        outputs = self.generate([release])
        import html
        for text in outputs.values():
            self.assertIn(subject, html.unescape(text))
            self.assertIn(release['name'], html.unescape(text))
            self.assertNotIn('<script>', text)
            self.assertNotIn('<img', text)
            self.assertNotIn('```', text)
            self.assertNotIn('{danger}', text)
            self.assertEqual(1, text.count('/commit/' + sha))
        self.assertEqual(outputs[OUTPUTS[0]].split('<!--', 1)[1],
                         outputs[OUTPUTS[1]].split('<!--', 1)[1])
        self.assertIn('<span dir="ltr">', outputs[OUTPUTS[2]])

    def test_rejects_missing_main_missing_tags_shallow_and_invalid_json(self):
        self.commit('Initial import')
        self.assertIn('fetch complete', self.generate([self.release('missing')], expected=1))
        self.assertIn('array', self.generate({}, expected=1))
        self.git('branch', '-m', 'feature')
        self.assertIn('fetch complete', self.generate([], expected=1))
        self.git('branch', 'main')
        (self.repo / '.git/shallow').write_text(self.git('rev-parse', 'HEAD') + '\n')
        self.assertIn('Shallow', self.generate([], expected=1))

    def test_empty_commit_subject_is_still_part_of_main_history(self):
        self.git('commit', '--allow-empty', '--allow-empty-message', '-m', '')
        sha = self.git('rev-parse', 'HEAD')
        for text in self.generate([]).values():
            self.assertEqual(1, text.count('/commit/' + sha))

    def test_drafts_and_unpublished_releases_do_not_require_tags(self):
        self.commit('Initial import')
        draft = self.release('missing-draft')
        draft['draft'] = True
        unpublished = self.release('missing-unpublished')
        unpublished['published_at'] = None
        self.assertEqual(self.generate([]), self.generate([draft, unpublished]))


if __name__ == '__main__':
    unittest.main()
