import fs from 'node:fs'
import path from 'node:path'

import { expect, test } from '@playwright/test'

import { expectNoA11yViolations } from './a11y-utils'

const DOCS_EXTENSIONS = ['.md', '.mdx']

type DocsRouteSets = {
  english: string[]
  arabic: string[]
  englishSlugs: string[]
  arabicSlugs: string[]
}

function listDocsSlugs(dir: string): string[] {
  const out: string[] = []

  const walk = (currentDir: string, prefix: string) => {
    for (const entry of fs.readdirSync(currentDir, { withFileTypes: true })) {
      if (entry.name.startsWith('.') || entry.name.startsWith('_')) continue

      const fullPath = path.join(currentDir, entry.name)
      if (entry.isDirectory()) {
        walk(fullPath, prefix ? `${prefix}/${entry.name}` : entry.name)
        continue
      }

      const extension = DOCS_EXTENSIONS.find((ext) => entry.name.endsWith(ext))
      if (!entry.isFile() || !extension) continue

      const name = entry.name.slice(0, -extension.length)
      const slug = prefix ? `${prefix}/${name}` : name
      out.push(slug)
    }
  }

  walk(dir, '')
  return out.sort()
}

function getDocsRouteSets(): DocsRouteSets {
  // Tests run from `web/` in CI.
  const docsRoot = path.resolve(process.cwd(), 'help/src/content/docs')
  const arRoot = path.join(docsRoot, 'ar')

  const englishSlugs = listDocsSlugs(docsRoot).filter((slug) => !slug.startsWith('ar/'))
  const arabicSlugs = listDocsSlugs(arRoot)

  const toRoute = (base: string, slug: string) => {
    if (slug === 'index') return `${base}/`
    return `${base}/${slug.replace(/\\/g, '/')}/`
  }

  const english = englishSlugs.map((slug) => toRoute('/help', slug))
  const arabic = arabicSlugs.map((slug) => toRoute('/help/ar', slug))

  return { english, arabic, englishSlugs, arabicSlugs }
}

const routes = getDocsRouteSets()

test.describe('Docs Site Accessibility @full', () => {
  test('Docs routes are in sync (EN/AR)', async () => {
    // We keep the docs bilingual; Arabic must mirror English route set.
    expect(routes.arabicSlugs).toEqual(routes.englishSlugs)
  })

  for (const route of routes.english) {
    test(`No accessibility violations (EN): ${route}`, async ({ page }) => {
      await page.goto(route)
      await page.waitForLoadState('networkidle')
      await expectNoA11yViolations(page)
    })
  }

  for (const route of routes.arabic) {
    test(`No accessibility violations (AR): ${route}`, async ({ page }) => {
      await page.goto(route)
      await page.waitForLoadState('networkidle')
      await expectNoA11yViolations(page)
    })
  }

  test('Arabic inline API and shortcut expressions retain LTR isolation', async ({ page }) => {
    await page.goto('/help/ar/developer-guide/library-api/')
    const signature = page.locator('main code').filter({ hasText: /^split_secret\(secret, k, n, passphrase\)$/ })
    await expect(signature).toHaveCount(1)
    await expect(signature).toHaveCSS('direction', 'ltr')
    await expect(signature).toHaveCSS('unicode-bidi', 'isolate')
    await page.goto('/help/ar/tui/')
    const shortcut = page.locator('main table span[dir="ltr"]').filter({ hasText: /^Ctrl\+L$/ })
    await expect(shortcut).toHaveCount(1)
    await expect(shortcut).toHaveCSS('direction', 'ltr')
    await page.goto('/help/ar/technical-design/')
    await expect(page.locator('main span[dir="ltr"]').filter({ hasText: /^k - 1$/ })).toBeVisible()
  })

  test('Docs are dark-only (no light theme toggle)', async ({ page }) => {
    await page.goto('/help/')
    
    // Wait for page to fully load
    await page.waitForLoadState('networkidle')
    
    // There should be no theme toggle button
    const themeToggle = page.locator('starlight-theme-select, [aria-label*="theme" i], [aria-label*="مظهر" i]')
    await expect(themeToggle).toHaveCount(0)
    
    // Verify dark theme is enforced via localStorage
    const starlightTheme = await page.evaluate(() => localStorage.getItem('starlight-theme'))
    expect(starlightTheme).toBe('dark')
    
    // Verify the page has dark theme styling applied
    const htmlDataTheme = await page.locator('html').getAttribute('data-theme')
    expect(htmlDataTheme).toBe('dark')
  })

  for (const locale of ['', 'ar/']) {
    const notice = locale ? 'علامة تبويب جديدة' : 'new tab'
    for (const slug of ['', 'getting-started/', 'web-ui/', 'technical-design/']) {
      test(`Predictable localized links: ${locale}${slug || 'index'}`, async ({ page }) => {
        await page.goto(`/help/${locale}${slug}`)
        const appLinks = page.locator('a[href="/"][target="_blank"]')
        expect(await appLinks.count()).toBeGreaterThan(0)
        for (const link of await appLinks.all()) {
          await expect(link).toContainText(notice)
          // Starlight also renders a hidden mobile copy of the social links.
          if (await link.isVisible()) {
            await expect(link).toHaveAccessibleName(new RegExp(notice))
          }
          await expect(link).toHaveAttribute('rel', /noopener/)
          await expect(link).toHaveAttribute('rel', /noreferrer/)
        }
        await expect(page.locator('a[target="_blank"]:not([href="/"])')).toHaveCount(0)
        const references = page.locator('a[href^="https://"]')
        expect(await references.count()).toBeGreaterThan(0)
        for (const link of await references.all()) {
          await expect(link).not.toHaveAttribute('target', '_blank')
        }
        const headerApp = page.locator('header a.app-link')
        await expect(headerApp).toHaveAccessibleName(
          locale ? 'افتح التطبيق (علامة تبويب جديدة)' : 'Open app (new tab)',
        )
      })
    }

    test(`App link preserves help with no opener: ${locale || 'en'}`, async ({ page, context }) => {
      await page.goto(`/help/${locale}web-ui/`)
      const helpUrl = page.url()
      const opened = context.waitForEvent('page')
      await page.locator('header a.app-link').click()
      const app = await opened
      await app.waitForLoadState('domcontentloaded')
      expect(new URL(app.url()).pathname).toBe('/')
      expect(await app.evaluate(() => window.opener === null)).toBe(true)
      await expect(page).toHaveURL(helpUrl)
      await app.close()
    })
  }
})
