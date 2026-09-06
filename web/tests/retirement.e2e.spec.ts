import { expect, test } from '@playwright/test'

for (const locale of ['', 'ar/']) {
  const root = `/help/${locale}`
  test(`downloads recommend supported terminal archives: ${root} @smoke`, async ({ page }) => {
    await page.goto(`${root}build-and-run/`)
    const main = page.locator('main')
    await expect(main).toContainText(locale
      ? 'أرشيفات CLI وTUI على Linux وWindows وmacOS'
      : 'CLI and TUI archives for Linux, Windows, and macOS')
    await expect(main).toContainText(locale ? 'مثبتات الواجهات الرسومية التاريخية غير مدعومة' : 'Historical GUI installers are unsupported')
    await expect(main.locator('a[href="../desktop/"]')).toBeVisible()
    await expect(main).not.toContainText(/tauri:|macos:package|Safeparts\.exe|AppImage|\.dmg|\.msi|safeparts_uniffi/)
  })

  test(`supported navigation and desktop migration: ${root} @smoke`, async ({ page }) => {
    await page.goto(root)
    await expect(page.locator('a[href$="/desktop/"], a[href="desktop/"]')).toHaveCount(0)
    for (const slug of ['web-ui', 'cli', 'tui']) {
      const link = page.locator(`main a[href="${slug}/"]`).first()
      await expect(link).toBeVisible()
      expect(await link.evaluate((anchor: HTMLAnchorElement) => new URL(anchor.href).pathname)).toBe(`${root}${slug}/`)
    }
    await page.goto(`${root}web-ui/`)
    await expect(page.locator('meta[name="description"]')).not.toHaveAttribute('content', /desktop|Tauri|سطح المكتب/)
    await expect(page.locator('nav.sidebar a[href$="/desktop/"]')).toHaveCount(0)
    for (const slug of ['web-ui', 'cli', 'tui']) {
      await expect(page.locator(`nav.sidebar a[href="${root}${slug}/"]`)).toBeVisible()
    }
    await expect(page.locator('main')).not.toContainText(/native.*workflow|التطبيقات الأصلية|ملفات أصلي/)
    const response = await page.goto(`${root}desktop/`)
    expect(response?.status()).toBe(200)
    await expect(page.getByRole('heading', { level: 1 })).toHaveText(
      locale ? 'توقف دعم تطبيقات سطح المكتب' : 'Desktop applications retired',
    )
    const main = page.locator('main')
    await expect(main).toContainText(locale ? 'لا يبطل حصص الاسترداد' : 'does not invalidate existing Recovery shares')
    await expect(main).toContainText(locale ? 'عبارات المرور' : 'passphrases')
    await expect(main).toContainText('UTF-8')
    await expect(main).toContainText(locale ? 'البايتات بدقة' : 'exact bytes')
    for (const slug of ['web-ui', 'cli', 'tui', 'it-devops-guide/break-glass']) {
      await expect(main.locator(`a[href*="${slug}/"]`).first()).toBeVisible()
    }
    await expect(main.locator('a[href*="releases/download"], a[href$=".dmg"], a[href$=".msi"]')).toHaveCount(0)
  })
}
