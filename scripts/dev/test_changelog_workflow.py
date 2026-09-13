#!/usr/bin/env python3
"""Guard the changelog writer and its existing artifact-workflow handoff."""
from pathlib import Path
import re
import unittest

import changelog

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
        self.assertIn('git rev-parse HEAD', text)
        self.assertIn('web-handoff-status --repository "$REPOSITORY" --source-sha "$source_sha"', text)
        self.assertIn('[ "$handoff_status" = dispatch ]', text)
        self.assertLess(text.index('git push origin HEAD:refs/heads/main'),
                        text.index('gh workflow run web-ci.yml --ref main'))
        paths = re.search(r'paths=\((.*?)\)', text, re.S).group(1).split()
        self.assertEqual(['CHANGELOG.md', 'web/help/src/content/docs/changelog.md',
                          'web/help/src/content/docs/ar/changelog.md'], paths)
        self.assertIn('set -euo pipefail', text)

    def test_unchanged_snapshot_skips_dispatch_when_intended_source_deployed(self):
        runs = {'workflow_runs': [dict(id=7, event='workflow_dispatch', head_sha='abc123', status='completed', conclusion='success')]}
        jobs = {7: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='success', steps=[
                dict(name='Check artifact is still latest main', conclusion='success'),
                dict(name='Check Netlify credentials', conclusion='success'),
                dict(name='Deploy artifact without a provider build', conclusion='success'),
                dict(name='Verify Netlify serves the artifact bytes', conclusion='success'),
            ]),
            dict(name='deploy tested artifact to Cloudflare Workers', conclusion='success', steps=[
                dict(name='Check artifact is still latest main', conclusion='success'),
                dict(name='Check Cloudflare credentials', conclusion='success'),
                dict(name='Deploy artifact without rebuilding source', conclusion='success'),
                dict(name='Verify Cloudflare serves the artifact bytes', conclusion='success'),
            ]),
        ]}}

        self.assertEqual('healthy', changelog.web_handoff_decision(runs, jobs, 'abc123'))

    def test_skipped_provider_verification_is_not_healthy_unless_credentials_disabled(self):
        runs = {'workflow_runs': [dict(id=9, event='workflow_dispatch', head_sha='abc123', status='completed', conclusion='success')]}
        skipped_after_stale_head = {9: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='success', steps=[
                dict(name='Check artifact is still latest main', conclusion='success'),
                dict(name='Check Netlify credentials', conclusion='skipped'),
                dict(name='Deploy artifact without a provider build', conclusion='skipped'),
                dict(name='Verify Netlify serves the artifact bytes', conclusion='skipped'),
            ]),
            dict(name='deploy tested artifact to Cloudflare Workers', conclusion='success', steps=[
                dict(name='Check artifact is still latest main', conclusion='success'),
                dict(name='Check Cloudflare credentials', conclusion='skipped'),
                dict(name='Deploy artifact without rebuilding source', conclusion='skipped'),
                dict(name='Verify Cloudflare serves the artifact bytes', conclusion='skipped'),
            ]),
        ]}}
        intentionally_disabled = {9: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='success', steps=[
                dict(name='Check artifact is still latest main', conclusion='success'),
                dict(name='Check Netlify credentials', conclusion='success'),
                dict(name='Deploy artifact without a provider build', conclusion='skipped'),
                dict(name='Verify Netlify serves the artifact bytes', conclusion='skipped'),
            ]),
            dict(name='deploy tested artifact to Cloudflare Workers', conclusion='success', steps=[
                dict(name='Check artifact is still latest main', conclusion='success'),
                dict(name='Check Cloudflare credentials', conclusion='success'),
                dict(name='Deploy artifact without rebuilding source', conclusion='skipped'),
                dict(name='Verify Cloudflare serves the artifact bytes', conclusion='skipped'),
            ]),
        ]}}

        self.assertEqual('dispatch', changelog.web_handoff_decision(runs, skipped_after_stale_head, 'abc123'))
        self.assertEqual('healthy', changelog.web_handoff_decision(runs, intentionally_disabled, 'abc123'))

    def test_latest_deployment_attempt_controls_handoff_health(self):
        runs = {'workflow_runs': [
            dict(id=20, event='workflow_dispatch', head_sha='abc123', status='completed', conclusion='failure'),
            dict(id=19, event='workflow_dispatch', head_sha='abc123', status='completed', conclusion='success'),
        ]}
        old_success_jobs = {19: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='success', steps=[
                dict(name='Verify Netlify serves the artifact bytes', conclusion='success'),
            ]),
            dict(name='deploy tested artifact to Cloudflare Workers', conclusion='success', steps=[
                dict(name='Verify Cloudflare serves the artifact bytes', conclusion='success'),
            ]),
        ]}}

        self.assertEqual('dispatch', changelog.web_handoff_decision(runs, old_success_jobs, 'abc123'))

    def test_scheduled_only_handoff_does_not_count_as_pending_publication(self):
        runs = {'workflow_runs': [dict(id=21, event='schedule', head_sha='abc123', status='in_progress', conclusion=None)]}

        self.assertEqual('dispatch', changelog.web_handoff_decision(runs, {}, 'abc123'))

    def test_failed_or_missing_handoff_requests_dispatch_for_intended_source(self):
        successful_old_run = {'workflow_runs': [dict(id=6, head_sha='old999', status='completed', conclusion='success')]}
        successful_old_jobs = {6: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='success'),
            dict(name='deploy tested artifact to Cloudflare Workers', conclusion='success'),
        ]}}
        failed_current_run = {'workflow_runs': [dict(id=8, head_sha='abc123', status='completed', conclusion='failure')]}
        skipped_deploy_jobs = {9: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='skipped'),
            dict(name='deploy tested artifact to Cloudflare Workers', conclusion='success'),
        ]}}
        missing_cloudflare = {10: {'jobs': [
            dict(name='deploy tested artifact to Netlify', conclusion='success'),
        ]}}

        self.assertEqual('dispatch', changelog.web_handoff_decision(successful_old_run, successful_old_jobs, 'abc123'))
        self.assertEqual('dispatch', changelog.web_handoff_decision(failed_current_run, {}, 'abc123'))
        self.assertEqual('dispatch', changelog.web_handoff_decision(
            {'workflow_runs': [dict(id=9, head_sha='abc123', status='completed', conclusion='success')]},
            skipped_deploy_jobs, 'abc123'))
        self.assertEqual('dispatch', changelog.web_handoff_decision(
            {'workflow_runs': [dict(id=10, head_sha='abc123', status='completed', conclusion='success')]},
            missing_cloudflare, 'abc123'))

    def test_in_progress_handoff_waits_instead_of_dispatching_duplicate_build(self):
        runs = {'workflow_runs': [dict(id=11, head_sha='abc123', status='in_progress', conclusion=None)]}

        self.assertEqual('pending', changelog.web_handoff_decision(runs, {}, 'abc123'))

    def test_branch_protection_docs_do_not_claim_workflow_scoped_github_token_bypass(self):
        text = (ROOT / 'docs/dev/branch-protection.md').read_text(encoding='utf-8')

        self.assertIn('GitHub Actions bot identity is shared across workflows', text)
        self.assertIn('do not treat a native actor bypass as workflow-scoped or file-scoped', text)
        self.assertIn('Route generated changelog snapshots through PRs', text)
        self.assertIn('Use a separately approved, least-privilege GitHub App', text)
        self.assertNotIn('allow only the existing changelog writer actor to update the three generated files', text)

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
        for step in ('Build application WASM', 'Test WASM browser bindings',
                     'Type-check Web application', 'Build Web application',
                     'Build English and Arabic help', 'Test the built Web and help'):
            self.assertIn(step, text)
        self.assertNotIn('Build desktop frontend', text)


if __name__ == '__main__':
    unittest.main()
