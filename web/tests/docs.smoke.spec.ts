import { expect, test } from '@playwright/test'
import { expectNoA11yViolations } from './a11y-utils'

const SMOKE_ROUTES = [
  '/help/',
  '/help/use-cases/',
  '/help/security/',
  '/help/changelog/',
  '/help/ar/',
  '/help/ar/use-cases/',
  '/help/ar/security/',
  '/help/ar/changelog/',
]

test.describe('Docs Accessibility Smoke @smoke', () => {
  for (const route of SMOKE_ROUTES) {
    test(`No accessibility violations: ${route}`, async ({ page }) => {
      await page.goto(route)
      await page.waitForLoadState('networkidle')
      await expectNoA11yViolations(page)
    })
  }

  for (const [route, title, direction] of [
    ['/help/changelog/', 'Changelog', 'ltr'],
    ['/help/ar/changelog/', 'سجل التغييرات', 'rtl'],
  ]) {
    test(`Changelog renders original history: ${route}`, async ({ page }) => {
      await page.goto(route)
      await expect(page.getByRole('heading', { level: 1, name: title, exact: true })).toBeVisible()
      await expect(page.locator('html')).toHaveAttribute('dir', direction)
      const content = page.locator('.sl-markdown-content')
      await expect(content).toContainText('feat: bootstrap Rust core and CLI MVP')
      await expect(content.locator('a[href="https://github.com/connectedloops/safeparts/releases/tag/v0.3.1"]')).toHaveCount(1)
      const links = content.locator('a[href*="/commit/"]')
      expect(await links.count()).toBeGreaterThanOrEqual(321)
      const hrefs = await links.evaluateAll((elements) => elements.map((el) => el.getAttribute('href')))
      expect(new Set(hrefs).size).toBe(hrefs.length)
      if (direction === 'rtl') {
        await expect(links.first().locator('..')).toHaveAttribute('dir', 'ltr')
      }
    })
  }

  test('English custody guidance covers every storage and transport boundary', async ({ page }) => {
    await page.goto('/help/security/')
    await expect(page.locator('main')).toContainText(
      'Keep fewer Recovery shares than the Threshold in every account, device, location, administrator domain, and transport channel.',
    )
  })

  test('Arabic custody guidance covers every storage and transport boundary', async ({ page }) => {
    await page.goto('/help/ar/security/')
    await expect(page.locator('main')).toContainText(
      'احتفظ بعدد من حصص الاسترداد أقل من العتبة في كل حساب وجهاز وموقع ونطاق إدارة وقناة نقل.',
    )
  })
})
