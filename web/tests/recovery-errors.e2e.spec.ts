import { readFile } from 'node:fs/promises'

import { expect, test } from '@playwright/test'

import { expectNoA11yViolations, waitForWasmReady } from './a11y-utils'

import initWasm, { combine_share_input, combine_shares, split_secret } from '../src/wasm_pkg/safeparts_wasm.js'

test.beforeAll(async () => {
  const bytes = await readFile(new URL('../src/wasm_pkg/safeparts_wasm_bg.wasm', import.meta.url))
  await initWasm({ module_or_path: Uint8Array.from(bytes) })
})

function failure(operation: () => unknown): unknown {
  try { operation() } catch (error) { return error }
  throw new Error('Expected recovery to fail')
}

test('public WASM recovery errors expose only stable codes and safe counts', () => {
  const shares = split_secret(new TextEncoder().encode('synthetic recovery boundary'), 3, 3, 'base64url') as string[]
  expect(failure(() => combine_shares(shares.slice(0, 1), 'base64url'))).toEqual({ code: 'insufficient_shares', required: 3, provided: 1 })
  expect(failure(() => combine_shares([shares[0], shares[0], shares[1]], 'base64url'))).toEqual({ code: 'duplicate_share', coordinate: 1 })
  const other = split_secret(new TextEncoder().encode('synthetic other set'), 3, 3, 'base64url') as string[]
  expect(failure(() => combine_shares([shares[0], shares[1], other[2]], 'base64url'))).toEqual({ code: 'inconsistent_shares' })
  const protectedShares = split_secret(new TextEncoder().encode('synthetic protected boundary'), 2, 2, 'base64url', 'synthetic passphrase') as string[]
  expect(failure(() => combine_shares(protectedShares, 'base64url'))).toEqual({ code: 'passphrase_required' })
  expect(failure(() => combine_shares(protectedShares, 'base64url', 'synthetic wrong passphrase'))).toEqual({ code: 'decryption_failed' })
})

for (const locale of ['en', 'ar'] as const) {
  test(`${locale}: unexpected boundary failures and legacy strings use a safe fallback`, async ({ page }) => {
    const errors: unknown[] = [
      'SECRET synthetic legacy exception',
      { code: 'future_code', message: 'SECRET synthetic exception' },
      { code: 'insufficient_shares', required: 'SECRET', provided: 1 },
      { code: 'insufficient_shares', required: 999, provided: -1 },
    ]
    for (const error of errors) {
      await page.route('**/src/wasm_pkg/safeparts_wasm.js*', route => route.fulfill({
        contentType: 'application/javascript',
        body: `export default async function init() {}\nexport function combine_share_input() { throw ${JSON.stringify(error)}; }`,
      }))
      await page.goto('/')
      if (locale === 'ar') await page.getByRole('button', { name: 'العربية' }).click()
      await page.getByRole('tab', { name: /combine|استعادة/i }).click()
      const panel = page.locator('#combine-panel')
      await panel.locator('textarea').first().fill('synthetic input')
      await panel.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
      await expect(panel.locator('.alert-error')).toContainText(locale === 'en' ? 'Recovery could not finish' : 'تعذر إكمال الاستعادة')
      await expect(panel.locator('.alert-error')).not.toContainText('SECRET')
      await expect(panel.locator('textarea[aria-invalid="true"]')).toHaveCount(0)
      await page.unroute('**/src/wasm_pkg/safeparts_wasm.js*')
    }
  })

  test(`${locale}: Recovery-share accessible names follow visible field order`, async ({ page }) => {
    await page.goto('/')
    await waitForWasmReady(page)
    if (locale === 'ar') await page.getByRole('button', { name: 'العربية' }).click()
    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const panel = page.locator('#combine-panel')
    const name = locale === 'en' ? 'Recovery share' : 'حصة استرداد'
    await expect(panel.getByRole('textbox', { name: `${name} 1`, exact: true })).toBeVisible()
    await expect(panel.getByRole('textbox', { name: `${name} 2`, exact: true })).toBeVisible()
    await panel.getByRole('button', { name: locale === 'en' ? 'Add share' : 'إضافة حصة', exact: true }).click()
    await panel.getByRole('textbox', { name: `${name} 3`, exact: true }).fill('synthetic third field')
    await panel.getByRole('button', { name: locale === 'en' ? 'Remove' : 'حذف', exact: true }).nth(1).click()
    await expect(panel.getByRole('textbox', { name: `${name} 2`, exact: true })).toHaveValue('synthetic third field')
    await expect(panel.getByRole('textbox', { name: `${name} 3`, exact: true })).toHaveCount(0)
    await expectNoA11yViolations(page)
  })

  test(`${locale}: recovery errors give safe localized actions through real WASM`, async ({ page }) => {
    const diagnostics: string[] = []
    page.on('console', message => diagnostics.push(message.text()))
    page.on('pageerror', error => diagnostics.push(error.message))
    await page.goto('/')
    await waitForWasmReady(page)
    if (locale === 'ar') await page.getByRole('button', { name: 'العربية' }).click()
    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const panel = page.locator('#combine-panel')
    const fields = panel.locator('textarea')
    const submit = panel.getByRole('button', { name: /^(combine|استعادة)$/i })
    const alert = panel.locator('.alert-error')
    await fields.nth(0).fill('SYNTHETIC-SENSITIVE-INPUT')
    await submit.click()
    await expect(alert).toContainText(locale === 'en' ? 'Check that each Recovery share is complete' : 'تحقق من اكتمال كل حصة استرداد')
    await expect(alert).not.toContainText('SYNTHETIC-SENSITIVE-INPUT')
    const thresholdThree = split_secret(new TextEncoder().encode('synthetic missing count'), 3, 3, 'mnemo-words') as string[]
    await fields.nth(0).fill(thresholdThree[0])
    await submit.click()
    await expect(alert).toHaveText(locale === 'en' ? 'Add 2 more shares to recover this secret.' : 'أضف 2 حصص أخرى لاستعادة هذا السر.')
    const shares = split_secret(new TextEncoder().encode('synthetic UI recovery'), 2, 3, 'mnemo-words') as string[]
    await fields.nth(0).fill(shares[0])
    await submit.click()
    await expect(alert).toHaveText(locale === 'en' ? 'Add 1 more share to recover this secret.' : 'أضف حصة واحدة أخرى لاستعادة هذا السر.')
    await expect(fields.nth(1)).toHaveAttribute('aria-invalid', 'true')
    await fields.nth(1).fill(shares[0])
    await submit.click()
    await expect(alert).toContainText(locale === 'en' ? 'Each Recovery share must be different' : 'يجب أن تكون كل حصة استرداد مختلفة')
    const protectedShares = split_secret(new TextEncoder().encode('synthetic protected UI'), 2, 2, 'mnemo-words', 'synthetic correct passphrase') as string[]
    await fields.nth(0).fill(protectedShares[0])
    await fields.nth(1).fill(protectedShares[1])
    await submit.click()
    await expect(alert).toContainText(locale === 'en' ? 'Enter the passphrase used' : 'أدخل عبارة المرور المستخدمة')
    await panel.locator('#recover-passphrase').fill('synthetic wrong passphrase')
    await submit.click()
    await expect(alert).toContainText(locale === 'en' ? 'The passphrase may be wrong or the encrypted data may have been changed' : 'قد تكون عبارة المرور خاطئة أو ربما تغيرت البيانات المشفرة')
    await expect(alert).not.toContainText('synthetic wrong passphrase')
    await fields.nth(0).fill(shares[0])
    await fields.nth(1).fill(protectedShares[1])
    await submit.click()
    await expect(alert).toContainText(locale === 'en' ? 'do not form one consistent set' : 'لا تنتمي إلى مجموعة متسقة واحدة')
    await panel.locator('label').filter({ hasText: /Letters|أحرف/i }).click()
    const unsupported = Buffer.from((split_secret(new TextEncoder().encode('synthetic unsupported UI'), 2, 2, 'base64url') as string[])[0], 'base64url')
    unsupported[5] = 0x80
    await fields.nth(0).fill(unsupported.toString('base64url'))
    await fields.nth(1).fill('')
    await submit.click()
    await expect(alert).toContainText(locale === 'en' ? 'unsupported packet or encryption parameters' : 'معاملات غير مدعومة للحزمة أو التشفير')
    const logged = diagnostics.join('\n')
    for (const sensitive of ['SYNTHETIC-SENSITIVE-INPUT', 'synthetic wrong passphrase', ...protectedShares]) {
      expect(logged.includes(sensitive)).toBe(false)
    }
  })
}

test('public WASM rejects unsupported packet and crypto parameters without leaking metadata', () => {
  const shares = split_secret(new TextEncoder().encode('synthetic parameter fixture'), 2, 2, 'base64url', 'synthetic passphrase') as string[]
  const flags = Buffer.from(shares[0], 'base64url')
  flags[5] = 0x80
  expect(failure(() => combine_share_input(flags.toString('base64url'), 'base64url'))).toEqual({ code: 'unsupported_parameters' })
  const crypto = Buffer.from(shares[0], 'base64url')
  // V2: 25-byte header, 16-byte salt, 12-byte nonce, then big-endian memory cost.
  crypto.writeUInt32BE(0xffffffff, 53)
  expect(failure(() => combine_share_input(crypto.toString('base64url'), 'base64url'))).toEqual({ code: 'unsupported_parameters' })
  const version = Buffer.from(shares[0], 'base64url')
  version[4] = 255
  // Core deliberately groups unsupported versions with malformed packets.
  expect(failure(() => combine_share_input(version.toString('base64url'), 'base64url'))).toEqual({ code: 'invalid_share' })
})

test('public WASM recovery errors redact malformed input and encoding names', () => {
  const sensitive = 'SYNTHETIC-SENSITIVE-INPUT'
  expect(failure(() => combine_share_input(sensitive, 'mnemo-words'))).toEqual({ code: 'invalid_share' })
  expect(failure(() => combine_share_input(sensitive, sensitive))).toEqual({ code: 'unsupported_encoding' })
  expect(failure(() => combine_shares([42], 'auto'))).toEqual({ code: 'invalid_share' })
})
