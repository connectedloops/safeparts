import { expect, test, type Locator } from '@playwright/test'

import { waitForWasmReady } from './a11y-utils'

async function expectWritingAssistanceOff(field: Locator) {
  await expect(field).toHaveJSProperty('spellcheck', false)
  await expect(field).toHaveAttribute('spellcheck', 'false')
  await expect(field).toHaveAttribute('autocorrect', 'off')
  await expect(field).toHaveAttribute('autocapitalize', 'none')
}

for (const language of ['en', 'ar'] as const) {
  test(`sensitive inputs preserve typing, Unicode, paste, clear, and encoding detection in ${language} @smoke`, async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    await page.goto('/')
    await waitForWasmReady(page)
    if (language === 'ar') await page.getByRole('button', { name: 'العربية' }).click()

    const secretField = page.locator('#split-panel textarea')
    const typed = '  MiXeD synthetic  '
    const unicode = '\nسِر تجريبي 🔐 e\u0301 É\t  '
    const secret = typed + unicode
    await secretField.pressSequentially(typed)
    await page.keyboard.insertText(unicode)
    await expect(secretField).toHaveValue(secret)
    await page.getByRole('button', { name: /^(Clear secret|مسح السر)$/ }).click()
    await expect(secretField).toHaveValue('')
    await page.evaluate((text) => navigator.clipboard.writeText(text), secret)
    await page.getByRole('button', { name: /^(Paste secret|لصق السر)$/ }).click()
    await expect(secretField).toHaveValue(secret)

    await page.locator('#split-n').fill('3')
    await page.locator('#split-k').fill('3')
    await page.locator('#split-panel label').filter({ hasText: /Letters|أحرف/i }).click()
    await page.getByRole('button', { name: /^(split|قسم)$/i }).click()
    const outputs = page.locator('#split-panel div[dir="ltr"].input')
    await expect(outputs).toHaveCount(3)
    const shares = await outputs.allTextContents()

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const fields = page.locator('#combine-panel textarea')
    await fields.first().pressSequentially(typed)
    await page.keyboard.insertText(unicode)
    await expect(fields.first()).toHaveValue(secret)
    await page.getByRole('button', { name: /^(Clear share|مسح الحصة) 1$/ }).click()
    await expect(fields.first()).toHaveValue('')

    // Paste via the app control, then the browser's native clipboard shortcut.
    const paddedShare = ` \t${shares[0]}\n  `
    await page.evaluate((text) => navigator.clipboard.writeText(text), paddedShare)
    await page.getByRole('button', { name: /^(Paste share|لصق الحصة) 1$/ }).click()
    await expect(fields.first()).toHaveValue(paddedShare)
    await expect(page.locator('#combine-panel input[value="base64url"]')).toBeChecked()
    // Threshold inspection adds a field through a second dynamic creation path.
    await expect(fields).toHaveCount(3)
    await expectWritingAssistanceOff(fields.nth(2))
    await page.evaluate((text) => navigator.clipboard.writeText(text), shares[1])
    await fields.nth(1).focus()
    await page.keyboard.press('ControlOrMeta+V')
    await expect(fields.nth(1)).toHaveValue(shares[1])
    await fields.nth(2).fill(shares[2])
    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    const recovered = page.locator('#combine-panel div[dir="auto"].input')
    await expect(recovered).toHaveCount(1)
    expect(await recovered.textContent()).toBe(secret)
    await expect(fields.first()).toHaveValue(paddedShare)
  })

  test(`sensitive inputs disable writing assistance in ${language} @smoke`, async ({ page }) => {
    await page.goto('/')
    await waitForWasmReady(page)
    if (language === 'ar') await page.getByRole('button', { name: 'العربية' }).click()

    await expectWritingAssistanceOff(page.locator('#split-panel textarea'))
    await expectWritingAssistanceOff(page.locator('#split-passphrase'))
    await page.locator('#split-passphrase').fill('synthetic-passphrase')
    await expectWritingAssistanceOff(page.locator('#split-passphrase-confirmation'))

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    await expectWritingAssistanceOff(page.locator('#recover-passphrase'))
    const shares = page.locator('#combine-panel textarea')
    await expect(shares).toHaveCount(2)
    for (const field of await shares.all()) await expectWritingAssistanceOff(field)
    await page.getByRole('button', { name: /^(Add share|إضافة حصة)$/ }).click()
    await expect(shares).toHaveCount(3)
    await expectWritingAssistanceOff(shares.nth(2))
  })
}
