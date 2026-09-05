import { expect, test, type Locator, type Page } from '@playwright/test'

import { waitForWasmReady } from './a11y-utils'

async function selectCompleteText(block: Locator): Promise<string> {
  return block.evaluate((element) => {
    const selection = window.getSelection()!
    const range = document.createRange()
    range.selectNodeContents(element)
    selection.removeAllRanges()
    selection.addRange(range)
    return selection.toString()
  })
}

async function clipboard(page: Page): Promise<string> {
  return page.evaluate(() => navigator.clipboard.readText())
}

test('Recovery shares are immediately selectable once, including a synthetic 4 KiB Secret @smoke', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write'])
  await page.goto('/')
  await waitForWasmReady(page)
  const secret = 'synthetic-4KiB! '.repeat(274).slice(0, 4096)
  expect(new TextEncoder().encode(secret)).toHaveLength(4096)
  await page.locator('#split-panel textarea').fill(secret)
  await page.locator('#split-panel').getByRole('button', { name: /^Split$/ }).click()
  const blocks = page.locator('#split-panel div[dir="ltr"].input')
  await expect(blocks).toHaveCount(3)
  const shares: string[] = []
  for (let i = 0; i < 3; i++) {
    await page.getByRole('button', { name: `Copy Recovery share ${i + 1}`, exact: true }).click()
    const value = await clipboard(page)
    expect(value.length).toBeGreaterThan(4096)
    shares.push(value)
    expect(await selectCompleteText(blocks.nth(i))).toBe(value)
    expect(await blocks.nth(i).innerText()).toBe(value)
    expect(await blocks.nth(i).locator('*').count()).toBeLessThanOrEqual(2)
  }
  await page.evaluate(() => navigator.clipboard.writeText('synthetic-sentinel'))
  await page.keyboard.press('Control+Shift+C')
  expect(await clipboard(page)).toBe('synthetic-sentinel')
  await expect(page.locator('#split-panel').getByRole('button', { name: /copy/i })).toHaveCount(3)

  // Validate the copied values through recovery, not a second DOM copy.
  await page.getByRole('tab', { name: /combine/i }).click()
  await page.locator('#combine-panel textarea').nth(0).fill(shares[0])
  await page.locator('#combine-panel textarea').nth(1).fill(shares[1])
  await page.locator('#combine-panel').getByRole('button', { name: /^Combine$/ }).click()
  const recovered = page.locator('#combine-panel div[dir="auto"].input')
  await expect(recovered).toBeVisible()
  expect(await selectCompleteText(recovered)).toBe(secret)
  expect(await recovered.innerText()).toBe(secret)
  expect(await recovered.locator('*').count()).toBeLessThanOrEqual(2)
  await page.getByRole('button', { name: 'Copy recovered Secret', exact: true }).click()
  expect(await clipboard(page)).toBe(secret)
})

for (const lang of ['en', 'ar'] as const) {
  for (const reducedMotion of ['no-preference', 'reduce'] as const) {
    test(`recovered Secret selection and copy are exact (${lang}, ${reducedMotion}) @smoke`, async ({ page, context }) => {
      await context.grantPermissions(['clipboard-read', 'clipboard-write'])
      await page.emulateMedia({ reducedMotion })
      await page.goto('/')
      await waitForWasmReady(page)
      if (lang === 'ar') await page.getByRole('button', { name: 'العربية' }).click()
      const secret = '\uFEFF \tالعربية עברית 🌍 e\u0301\u0000\nsynthetic Secret \t\n '
      await page.locator('#split-panel textarea').fill(secret)
      await page.locator('#split-panel').getByRole('button', { name: /^(Split|قسم)$/ }).click()
      const shares: string[] = []
      for (let i = 0; i < 2; i++) {
        await page.getByRole('button', { name: new RegExp(`^(Copy Recovery share|نسخ حصة الاسترداد) ${i + 1}$`) }).click()
        shares.push(await clipboard(page))
        expect(await selectCompleteText(page.locator('#split-panel div[dir="ltr"].input').nth(i))).toBe(shares[i])
      }
      await page.getByRole('tab', { name: /combine|استعادة/i }).click()
      await page.locator('#combine-panel textarea').nth(0).fill(shares[0])
      await page.locator('#combine-panel textarea').nth(1).fill(shares[1])
      await page.locator('#combine-panel').getByRole('button', { name: /^(Combine|استعادة)$/ }).click()
      const block = page.locator('#combine-panel div[dir="auto"].input')
      await expect(block).toBeVisible()
      expect(await selectCompleteText(block)).toBe(secret)
      expect(await block.innerText()).toBe(secret)
      expect(await block.locator('*').count()).toBeLessThanOrEqual(2)
      expect(await block.ariaSnapshot()).toContain('synthetic Secret')
      expect(await block.evaluate((element) => element.closest('[aria-live], [role="status"], [aria-hidden="true"]') !== null)).toBe(false)
      const live = page.locator('[aria-live="polite"]')
      await expect(live).toContainText(lang === 'en' ? 'Secret recovered.' : 'تمت استعادة السر.')
      await expect(live).not.toContainText('synthetic Secret')

      // Presentation changes cannot become the explicit copy source.
      await block.evaluate((element) => { element.textContent = 'synthetic presentation sentinel' })
      await page.getByRole('button', { name: /^(Copy recovered Secret|نسخ السر المستعاد)$/ }).click()
      expect(await clipboard(page)).toBe(secret)
      await page.evaluate(() => navigator.clipboard.writeText('synthetic-sentinel'))
      await page.keyboard.press('Control+Shift+C')
      await expect.poll(() => clipboard(page)).toBe(secret)
      await page.locator('#combine-panel textarea').nth(0).fill('changed synthetic input')
      await expect(block).toHaveCount(0)
      await page.evaluate(() => navigator.clipboard.writeText('synthetic-sentinel'))
      await page.keyboard.press('Control+Shift+C')
      expect(await clipboard(page)).toBe('synthetic-sentinel')
    })
  }
}
