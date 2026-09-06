// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// https://astro.build/config
export default defineConfig({
	devToolbar: { enabled: false },
	// This Starlight site is deployed under the main web UI at `/help/`.
	base: '/help',
	// Build into the Vite app's dist directory so Netlify can publish both.
	outDir: '../dist/help',
	integrations: [
		starlight({
			// Keep these paths correct even when deployed under `base`.
			// Starlight does not automatically prefix absolute URLs with `base`.
			// (e.g. `/favicon.svg` would resolve to the domain root, not `/help/favicon.svg`).
			//
			// If you change `base`, update the explicit `/help/...` paths below.
			title: { en: 'Safeparts Help', ar: 'مساعدة Safeparts' },
			description:
				'Recover critical secrets without keeping them in one place. Guides for web, desktop, CLI, and TUI.',
			defaultLocale: 'root',
			locales: {
				root: { label: 'English', lang: 'en' },
				ar: { label: 'العربية', lang: 'ar', dir: 'rtl' },
			},
			logo: {
				src: './src/assets/logo.svg',
				alt: 'Safeparts',
			},
			// Starlight prefixes the configured `base` to this path.
			// Use a root-relative path so the final URL becomes `/help/favicon.svg`.
			favicon: '/favicon.svg',
			social: [
				{
					icon: 'github',
					label: 'GitHub',
					href: 'https://github.com/connectedloops/safeparts',
				},
				{
					icon: 'discord',
					label: 'Discord',
					href: 'https://discord.gg/ZaSfpcy8At',
				},
			],
			customCss: ['./src/styles/theme.css'],
			components: {
				SocialIcons: './src/components/SocialIcons.astro',
				Footer: './src/components/Footer.astro',
			},
			head: [
				{ tag: 'meta', attrs: { name: 'color-scheme', content: 'dark' } },
				{ tag: 'meta', attrs: { name: 'theme-color', content: '#000000' } },
				{
					tag: 'script',
					content: `
;(() => {
  // Keep docs consistent with the main app (dark-only). Some mobile browsers
  // default to light mode which makes callouts (note/warning/etc) too bright.
  try {
    localStorage.setItem('starlight-theme', 'dark');
  } catch {
    // ignore
  }

  // Ensure the attribute-based theme is also dark.
  // (Some tests assert on this, and it avoids a flash of light theme.)
  try {
    document.documentElement.setAttribute('data-theme', 'dark');
  } catch {
    // ignore
  }

  // Starlight still renders a theme select UI by default; remove it entirely.
  // (CSS hiding keeps it in the DOM, which breaks our dark-only tests.)
  const removeThemeUi = () => {
    for (const el of document.querySelectorAll('starlight-theme-select')) {
      el.remove();
    }
  };
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', removeThemeUi, { once: true });
  } else {
    removeThemeUi();
  }
})();
`,
				},
				{
					tag: 'link',
					attrs: { rel: 'apple-touch-icon', href: '/help/apple-touch-icon.png' },
				},
				{ tag: 'link', attrs: { rel: 'manifest', href: '/help/site.webmanifest' } },
				{
					tag: 'script',
					attrs: { type: 'module' },
					content: `
const isExternal = (href) => href.startsWith('http://') || href.startsWith('https://');

for (const a of document.querySelectorAll('a[href]')) {
  const href = a.getAttribute('href');
  if (!href) continue;
  if (href.startsWith('#')) continue;
  if (href.startsWith('mailto:') || href.startsWith('tel:')) continue;

  const isNonDocsRootPath = href.startsWith('/') && !href.startsWith('/help');
  if (isExternal(href) || isNonDocsRootPath) {
    a.setAttribute('target', '_blank');
    const rel = a.getAttribute('rel') ?? '';
    const relParts = new Set(rel.split(/\s+/).filter(Boolean));
    relParts.add('noopener');
    relParts.add('noreferrer');
    a.setAttribute('rel', Array.from(relParts).join(' '));
  }
}
`,
				},
				{ tag: 'link', attrs: { rel: 'preconnect', href: 'https://fonts.googleapis.com' } },
				{
					tag: 'link',
					attrs: { rel: 'preconnect', href: 'https://fonts.gstatic.com', crossorigin: '' },
				},
				{
					tag: 'link',
					attrs: {
						rel: 'stylesheet',
						href:
							'https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@400;500;600;700&family=IBM+Plex+Sans+Arabic:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;600;700&display=swap',
					},
				},
			],
			sidebar: [
				{
					label: 'Start',
					translations: { ar: 'البداية' },
					items: [{ slug: 'getting-started' }, { slug: 'use-cases' }, { slug: 'security' }],
				},
				{
					label: 'Use Safeparts',
					translations: { ar: 'استخدام Safeparts' },
					items: [{ slug: 'web-ui' }, { slug: 'desktop' }, { slug: 'cli' }, { slug: 'tui' }],
				},
				{
					label: 'Operations',
					translations: { ar: 'العمليات' },
					items: [
						{ slug: 'it-devops-guide' },
						{ slug: 'it-devops-guide/cli-runbooks' },
						{ slug: 'it-devops-guide/break-glass' },
						{ slug: 'it-devops-guide/automation' },
					],
				},
				{
					label: 'Reference',
					translations: { ar: 'مراجع' },
					items: [
						{ slug: 'encodings' },
						{ slug: 'project' },
						{ slug: 'changelog' },
						{ slug: 'technical-design' },
						{ slug: 'build-and-run' },
						{ slug: 'troubleshooting' },
					],
				},
				{
					label: 'Developers',
					translations: { ar: 'المطورون' },
					items: [
						{ slug: 'developer-guide' },
						{ slug: 'developer-guide/library-api' },
						{ slug: 'developer-guide/encodings-and-packets' },
						{ slug: 'developer-guide/testing-and-errors' },
					],
				},
			],
		}),
	],
});
