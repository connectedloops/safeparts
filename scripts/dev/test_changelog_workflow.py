#!/usr/bin/env python3
"""Guard the changelog writer and its existing artifact-workflow handoff."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ChangelogWorkflowTests(unittest.TestCase):
    def test_writer_has_trusted_main_context_and_scoped_permissions(self):
        text = (ROOT / '.github/workflows/changelog.yml').read_text()
        self.assertIn('branches: [main]', text)
        self.assertIn('workflows: [release]', text)
        self.assertIn('types: [completed]', text)
        self.assertIn('schedule:', text)
        self.assertIn('workflow_dispatch:', text)
        guard = text.split('    if: >-\n', 1)[1].split('    runs-on:', 1)[0]
        self.assertTrue(guard.lstrip().startswith("github.repository == 'connectedloops/safeparts' &&\n"))
        self.assertIn("github.ref == 'refs/heads/main'", guard)
        self.assertIn("github.event.workflow_run.conclusion == 'success'", text)
        self.assertIn("github.event.workflow_run.event == 'push'", text)
        self.assertIn('github.event.workflow_run.head_repository.full_name == github.repository', text)
        self.assertIn('permissions:\n  contents: read', text)
        self.assertEqual(1, text.count('contents: write'))
        self.assertEqual(1, text.count('actions: write'))
        self.assertNotIn('pull_request:', text)
        self.assertNotIn('secrets.', text)
        self.assertIn('ref: main', text)
        self.assertIn('fetch-depth: 0', text)
        self.assertIn('cancel-in-progress: false', text)
        for ref in re.findall(r'uses: (\S+)', text):
            self.assertRegex(ref, r'@[0-9a-f]{40}$')

    def test_writer_allows_only_snapshots_and_dispatches_after_nonforce_push(self):
        text = (ROOT / '.github/workflows/changelog.yml').read_text()
        self.assertIn('gh api --paginate --slurp', text)
        self.assertIn('--main-ref main --releases-json', text)
        self.assertIn('git add -- "${paths[@]}"', text)
        self.assertIn('git diff --cached --quiet', text)
        self.assertIn('git push origin HEAD:refs/heads/main', text)
        self.assertNotIn('--force', text)
        self.assertNotIn('git add .', text)
        self.assertNotIn('git commit -a', text)
        self.assertIn('chore(changelog): update generated history', text)
        self.assertIn('41898282+github-actions[bot]@users.noreply.github.com', text)
        self.assertIn('gh workflow run web-ci.yml --ref main', text)
        self.assertLess(text.index('git push origin HEAD:refs/heads/main'),
                        text.index('gh workflow run web-ci.yml --ref main'))
        paths = re.search(r'paths=\((.*?)\)', text, re.S).group(1).split()
        self.assertEqual(['CHANGELOG.md', 'web/help/src/content/docs/changelog.md',
                          'web/help/src/content/docs/ar/changelog.md'], paths)
        self.assertIn('set -euo pipefail', text)

    def test_web_deploy_manual_refresh_is_main_only_with_existing_gates(self):
        text = (ROOT / '.github/workflows/web-ci.yml').read_text()
        guard = "if: (github.event_name == 'push' || github.event_name == 'workflow_dispatch') && github.ref == 'refs/heads/main'"
        for name in ('deploy_netlify', 'deploy_cloudflare'):
            job = text.split('  ' + name + ':\n', 1)[1].split('\n  deploy_', 1)[0]
            self.assertIn(guard, job)
            self.assertIn('needs: [build_test_package, container_smoke]', job)
            self.assertIn('deploy-artifact.py verify', job)
        self.assertNotIn('actions: write', text)
        self.assertIn('bun run test:e2e:full', text)
        self.assertIn('Build desktop frontend', text)


if __name__ == '__main__':
    unittest.main()
