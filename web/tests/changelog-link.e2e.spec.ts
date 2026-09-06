import { expect, test } from '@playwright/test'

for (const arabic of [false, true]) {
  test(`footer opens the ${arabic ? 'Arabic' : 'English'} changelog without losing input @smoke`, async ({ page }) => {
    await page.goto('/')
    if (arabic) await page.getByRole('button', { name: 'العربية', exact: true }).click()
    const secret = page.locator('#split-panel textarea').first()
    await secret.fill('synthetic footer navigation test')

    const link = page.getByRole('contentinfo').getByRole('link', {
      name: arabic ? 'سجل التغييرات' : 'Changelog', exact: true,
    })
    await expect(link).toHaveAttribute('href', arabic ? '/help/ar/changelog/' : '/help/changelog/')
    await expect(link).toHaveAttribute('target', '_blank')
    await expect(link).toHaveAttribute('rel', 'noopener noreferrer')
    await link.focus()
    const popupPromise = page.waitForEvent('popup')
    await page.keyboard.press('Enter')
    const popup = await popupPromise
    await expect(popup.getByRole('heading', { level: 1 })).toHaveText(arabic ? 'سجل التغييرات' : 'Changelog')
    expect(await popup.evaluate(() => window.opener === null)).toBe(true)
    await expect(secret).toHaveValue('synthetic footer navigation test')
    await popup.close()
  })
}
