import { readFile } from 'node:fs/promises'

import { expect, test, type Page } from '@playwright/test'

import initWasm, { split_secret as splitSecret } from '../src/wasm_pkg/safeparts_wasm.js'
import { waitForWasmReady } from './a11y-utils'

async function instrumentClipboard(page: Page, shouldFail = false): Promise<void> {
  await page.evaluate((failWrites) => {
    const writes: string[] = []
    Object.defineProperty(window, '__clipboardWrites', {
      configurable: true,
      value: writes,
    })
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {
        writeText: async (text: string) => {
          if (failWrites) throw new Error('synthetic clipboard failure')
          writes.push(text)
        },
      },
    })
  }, shouldFail)
}

async function clipboardWrites(page: Page): Promise<string[]> {
  return page.evaluate(() => {
    return (window as Window & { __clipboardWrites?: string[] }).__clipboardWrites ?? []
  })
}

let nodeWasmReady: Promise<unknown> | undefined

async function splitSyntheticBytes(
  secret: number[],
  encoding = 'base64url',
): Promise<string[]> {
  nodeWasmReady ??= readFile(
    new URL('../src/wasm_pkg/safeparts_wasm_bg.wasm', import.meta.url),
  ).then((bytes) => initWasm({ module_or_path: Uint8Array.from(bytes) }))
  await nodeWasmReady
  return Array.from(splitSecret(new Uint8Array(secret), 2, 3, encoding)).map(String)
}

async function splitAndCollectShares(
  page: Page,
  secret: string,
  options?: { passphrase?: string },
): Promise<string[]> {
  await page.getByRole('tab', { name: /split|تقسيم/i }).click()
  await page.locator('#split-panel textarea').first().fill(secret)

  if (options?.passphrase) {
    await page.getByLabel('Passphrase (optional)').fill(options.passphrase)
    await page.getByLabel('Confirm passphrase').fill(options.passphrase)
  }

  await page.getByRole('button', { name: /^(split|قسم)$/i }).click()
  await expect(page.getByRole('heading', { name: /shares|الحصص/i })).toBeVisible()

  return page.locator('#split-panel div[dir="ltr"].input').allTextContents()
}

async function recoverShares(
  page: Page,
  shares: string[],
  encoding: 'base64url' | 'mnemo-words' = 'mnemo-words',
): Promise<void> {
  await page.getByRole('tab', { name: /combine|استعادة/i }).click()
  await page
    .locator('#combine-panel label')
    .filter({
      hasText: encoding === 'base64url' ? /Letters|أحرف/i : /Words|كلمات/i,
    })
    .click()

  const shareFields = page.locator('#combine-panel textarea')
  await shareFields.nth(0).fill(shares[0] ?? '')
  await shareFields.nth(1).fill(shares[1] ?? '')
  await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
}

function recoveredSecret(page: Page) {
  return page.locator('#combine-panel div[dir="auto"].input')
}

test.describe('Web App E2E Smoke @smoke', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/')
    await waitForWasmReady(page)
  })

  test('keeps the background gradient fixed to the viewport as shares grow the page', async ({ page }) => {
    const gradient = page.getByTestId('background-gradient')
    const viewport = page.viewportSize()
    expect(viewport).not.toBeNull()

    const initialBounds = await gradient.boundingBox()
    expect(initialBounds).toEqual({ x: 0, y: 0, width: viewport?.width, height: viewport?.height })

    await page.locator('#split-panel textarea').first().fill('synthetic-background-layout-secret')
    await page.locator('#split-n').fill('20')
    await page.getByRole('button', { name: /^(split|قسم)$/i }).click()
    await expect(page.getByRole('heading', { name: /shares|الحصص/i })).toBeVisible()

    const expandedBounds = await gradient.boundingBox()
    expect(expandedBounds).toEqual(initialBounds)
  })

  test('round-trip flow works in the browser', async ({ page }) => {
    const secret = 'smoke-roundtrip-secret-123'
    const shares = await splitAndCollectShares(page, secret)

    expect(shares.length).toBeGreaterThanOrEqual(2)

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill(shares[1] ?? '')

    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(page.getByRole('heading', { name: /recovered secret|السر المستعاد/i })).toBeVisible()

    expect(await recoveredSecret(page).textContent()).toBe(secret)
  })

  test('preserves valid Unicode and embedded NUL text exactly', async ({ page }) => {
    const secret = 'Safeparts العربية עברית 🌍\u0000after-nul'
    const shares = await splitAndCollectShares(page, secret)

    await recoverShares(page, shares)

    await expect(page.getByRole('heading', { name: /recovered secret/i })).toBeVisible()
    await expect(recoveredSecret(page)).toHaveText(secret)

    const liveStatus = page.locator('[aria-live="polite"]')
    await expect(liveStatus).toContainText('Secret recovered.')
    await expect(liveStatus).not.toContainText(secret)
  })

  test('preserves every valid UTF-8 character in recovered output and shortcut copy', async ({
    page,
    context,
  }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    const secret = '\uFEFF \tSafeparts العربية עברית 🌍\u0000after-nul \n'
    const shares = await splitSyntheticBytes(
      Array.from(new TextEncoder().encode(secret)),
    )

    await recoverShares(page, shares, 'base64url')

    await expect(page.getByRole('heading', { name: /recovered secret/i })).toBeVisible()
    expect(await recoveredSecret(page).textContent()).toBe(secret)

    await page.evaluate(() => navigator.clipboard.writeText('clipboard-sentinel'))
    await page.keyboard.press('Control+Shift+C')
    await expect
      .poll(() => page.evaluate(() => navigator.clipboard.readText()))
      .toBe(secret)
  })

  test('removes recovered output and copy source when recovery inputs change', async ({ page }) => {
    const shares = await splitAndCollectShares(page, 'stale-recovery-output')
    const recoveredHeading = page.getByRole('heading', { name: /recovered secret/i })
    const copyButton = page
      .locator('#combine-panel')
      .getByRole('button', { name: /^Copy recovered Secret$/ })

    await recoverShares(page, shares)
    await expect(recoveredHeading).toBeVisible()
    await expect(copyButton).toBeVisible()

    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(`${shares[0]} changed`)
    await expect(recoveredHeading).toHaveCount(0)
    await expect(copyButton).toHaveCount(0)

    await shareFields.nth(0).fill(shares[0] ?? '')
    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(recoveredHeading).toBeVisible()
    await page.getByRole('button', { name: /add share/i }).click()
    await expect(recoveredHeading).toHaveCount(0)

    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(recoveredHeading).toBeVisible()
    await page.locator('#recover-passphrase').fill('changed')
    await expect(recoveredHeading).toHaveCount(0)

    await page.locator('#recover-passphrase').fill('')
    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(recoveredHeading).toBeVisible()
    await page
      .locator('#combine-panel label')
      .filter({ hasText: /Letters/i })
      .click()
    await expect(recoveredHeading).toHaveCount(0)
    await expect(copyButton).toHaveCount(0)
  })

  test('discards recovery that finishes after a Recovery share changes', async ({ page }) => {
    const shares = await splitSyntheticBytes(
      Array.from(new TextEncoder().encode('pending-recovery-result')),
    )
    let releaseWasm: (() => void) | undefined
    const wasmGate = new Promise<void>((resolve) => {
      releaseWasm = resolve
    })

    await page.route('**/safeparts_wasm_bg*.wasm*', async (route) => {
      await wasmGate
      await route.continue()
    })

    try {
      await page.reload({ waitUntil: 'domcontentloaded' })
      await page.getByRole('tab', { name: /combine|استعادة/i }).click()
      await page
        .locator('#combine-panel label')
        .filter({ hasText: /Letters/i })
        .click()
      const shareFields = page.locator('#combine-panel textarea')
      await shareFields.nth(0).fill(shares[0] ?? '')
      await shareFields.nth(1).fill(shares[1] ?? '')
      await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
      await expect(page.getByRole('button', { name: /Working/i })).toBeVisible()

      await shareFields.nth(0).fill(`${shares[0]} changed`)
      releaseWasm?.()

      await expect(page.getByRole('button', { name: /^Combine$/i })).toBeEnabled()
      await expect(page.getByRole('heading', { name: /recovered secret/i })).toHaveCount(0)
      await expect(
        page
          .locator('#combine-panel')
          .getByRole('button', { name: /^Copy recovered Secret$/ }),
      ).toHaveCount(0)
    } finally {
      releaseWasm?.()
      await page.unroute('**/safeparts_wasm_bg*.wasm*')
    }
  })

  test('refuses invalid UTF-8 without changing the clipboard', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    await page.evaluate(() => navigator.clipboard.writeText('clipboard-sentinel'))
    const shares = await splitSyntheticBytes([0xff, 0xfe, 0x41])

    await recoverShares(page, shares, 'base64url')

    await expect(page.locator('#combine-panel .alert-error')).toContainText(
      'This recovered Secret is not valid UTF-8 text. Use the CLI, TUI, or a native file workflow to recover the exact bytes.',
    )
    await expect(page.getByRole('heading', { name: /recovered secret/i })).toHaveCount(0)
    await expect(
      page
        .locator('#combine-panel')
        .getByRole('button', { name: /^Copy recovered Secret$/ }),
    ).toHaveCount(0)
    await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe(
      'clipboard-sentinel',
    )
  })

  test('localizes invalid UTF-8 guidance in Arabic', async ({ page }) => {
    const shares = await splitSyntheticBytes([0x80, 0x41])
    await page.getByRole('button', { name: 'العربية' }).click()

    await recoverShares(page, shares, 'base64url')

    await expect(page.locator('#combine-panel .alert-error')).toContainText(
      'السر المستعاد ليس نصًا صالحًا بترميز UTF-8. استخدم CLI أو TUI أو سير عمل ملفات أصليًا لاستعادة البايتات بدقة.',
    )
    await expect(page.getByRole('heading', { name: /السر المستعاد/i })).toHaveCount(0)
  })

  test('shows useful validation when shares are insufficient', async ({ page }) => {
    const shares = await splitAndCollectShares(page, 'smoke-insufficient-shares')

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill('')

    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(page.locator('#combine-panel .alert-error')).toContainText(/add 1 more share to recover this secret/i)
    await expect(page.locator('#combine-panel textarea[aria-invalid="true"]')).toHaveCount(1)
  })

  test('arabic combine share placeholder stays rtl while input stays ltr', async ({ page }) => {
    await page.getByRole('button', { name: 'العربية' }).click()
    await page.getByRole('tab', { name: /combine|استعادة/i }).click()

    const shareField = page.locator('#combine-panel textarea').first()
    const placeholder = page.locator('#combine-panel [aria-hidden="true"]').filter({ hasText: 'الصق الحصة هنا…' }).first()

    await expect(placeholder).toBeVisible()
    await expect(placeholder).toHaveAttribute('dir', 'rtl')
    await expect(shareField).toHaveAttribute('dir', 'ltr')

    const padding = await shareField.evaluate((node) => {
      const styles = window.getComputedStyle(node)
      return {
        left: styles.paddingLeft,
        right: styles.paddingRight,
      }
    })

    expect(padding).toEqual({ left: '12px', right: '48px' })
  })

  test('passphrase fields are masked and reveal controls are keyboard operable', async ({ page }) => {
    const splitPassphrase = page.getByLabel('Passphrase (optional)')
    const splitReveal = page.getByRole('button', { name: 'Show passphrase' })

    await expect(splitPassphrase).toHaveAttribute('type', 'password')
    await expect(splitPassphrase).toHaveAttribute('spellcheck', 'false')
    await expect(splitPassphrase).toHaveAttribute('autocorrect', 'off')
    await expect(splitPassphrase).toHaveAttribute('autocapitalize', 'none')
    await expect(splitReveal).toHaveAttribute('aria-pressed', 'false')

    await splitReveal.focus()
    await page.keyboard.press('Enter')
    await expect(splitPassphrase).toHaveAttribute('type', 'text')
    await expect(page.getByRole('button', { name: 'Hide passphrase' })).toHaveAttribute('aria-pressed', 'true')

    await page.keyboard.press('Space')
    await expect(splitPassphrase).toHaveAttribute('type', 'password')

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const recoverPassphrase = page.getByLabel('Passphrase (optional)')
    await expect(recoverPassphrase).toHaveAttribute('type', 'password')
    await expect(page.getByLabel('Confirm passphrase')).toHaveCount(0)
  })

  test('split requires an exact passphrase confirmation and invalidates old shares', async ({ page }) => {
    const splitButton = page.getByRole('button', { name: /^(split|قسم)$/i })
    await page.locator('#split-panel textarea').first().fill('confirmation-behavior-secret')
    await page.getByLabel('Passphrase (optional)').fill('case-sensitive-passphrase')

    await expect(page.getByLabel('Confirm passphrase')).toBeVisible()
    await expect(splitButton).toBeDisabled()
    await expect(page.getByText('Passphrases must match exactly.')).toBeVisible()

    await page.getByLabel('Confirm passphrase').fill('Case-sensitive-passphrase')
    await expect(splitButton).toBeDisabled()

    await page.getByLabel('Confirm passphrase').fill('case-sensitive-passphrase')
    await expect(splitButton).toBeEnabled()
    await splitButton.click()
    await expect(page.getByRole('heading', { name: 'Shares' })).toBeVisible()

    await page.getByLabel('Passphrase (optional)').fill('changed-passphrase')
    await expect(page.getByRole('heading', { name: 'Shares' })).toHaveCount(0)
  })

  test('passphrase paste and clear actions handle both split fields', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    await page.reload()
    await waitForWasmReady(page)
    await page.evaluate(() => navigator.clipboard.writeText('pasted-synthetic-passphrase'))

    await page.getByRole('button', { name: 'Paste passphrase' }).click()
    await expect(page.getByLabel('Passphrase (optional)')).toHaveValue('pasted-synthetic-passphrase')
    await expect(page.getByLabel('Confirm passphrase')).toBeVisible()

    await page.getByRole('button', { name: 'Paste passphrase confirmation' }).click()
    await expect(page.getByLabel('Confirm passphrase')).toHaveValue('pasted-synthetic-passphrase')

    await page.getByRole('button', { name: 'Clear passphrase confirmation' }).click()
    await expect(page.getByLabel('Confirm passphrase')).toHaveValue('')

    await page.getByRole('button', { name: 'Clear passphrase', exact: true }).click()
    await expect(page.getByLabel('Confirm passphrase')).toHaveCount(0)
  })

  test('confirmation edit, paste, and clear invalidate generated shares', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    await page.reload()
    await waitForWasmReady(page)

    const splitPanel = page.locator('#split-panel')
    const splitButton = splitPanel.getByRole('button', { name: /^(split|قسم)$/i })
    const confirmation = splitPanel.getByLabel('Confirm passphrase')
    const passphrase = 'synthetic-confirmation-passphrase'
    const sharesHeading = splitPanel.getByRole('heading', { name: /^recovery shares$/i })
    const shareCopyButtons = splitPanel.getByRole('button', { name: /^copy recovery share \d+$/i })
    const expectNoShares = async () => {
      await expect(sharesHeading).toHaveCount(0)
      await expect(shareCopyButtons).toHaveCount(0)
    }
    const generateShares = async () => {
      await splitButton.click()
      await expect(sharesHeading).toBeVisible()
    }

    await splitPanel.locator('textarea').fill('synthetic-confirmation-invalidation-secret')
    await splitPanel.getByLabel('Passphrase (optional)').fill(passphrase)
    await confirmation.fill(passphrase)

    await generateShares()
    await confirmation.fill('synthetic-edited-confirmation')
    await expectNoShares()

    await confirmation.fill(passphrase)
    await generateShares()
    await page.evaluate(() => navigator.clipboard.writeText('synthetic-pasted-confirmation'))
    await confirmation.press(`${process.platform === 'darwin' ? 'Meta' : 'Control'}+A`)
    await confirmation.press(`${process.platform === 'darwin' ? 'Meta' : 'Control'}+V`)
    await expect(confirmation).toHaveValue('synthetic-pasted-confirmation')
    await expectNoShares()

    await confirmation.fill(passphrase)
    await generateShares()
    await splitPanel.getByRole('button', { name: 'Clear passphrase confirmation' }).click()
    await expectNoShares()
  })

  test('Recover passphrase edit, paste, and clear invalidate recovered output', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    await page.reload()
    await waitForWasmReady(page)

    const passphrase = 'synthetic-recovery-invalidation-passphrase'
    const shares = await splitAndCollectShares(page, 'synthetic-recovery-invalidation-secret', { passphrase })
    await page.getByRole('tab', { name: /combine|استعادة/i }).click()

    const combinePanel = page.locator('#combine-panel')
    const shareFields = combinePanel.locator('textarea')
    const recoverPassphrase = combinePanel.getByLabel('Passphrase (optional)')
    const combineButton = combinePanel.getByRole('button', { name: /^(combine|استعادة)$/i })
    const expectNoRecoveredSecret = async () => {
      await expect(combinePanel.getByRole('heading', { name: /recovered secret|السر المستعاد/i })).toHaveCount(0)
      await expect(combinePanel.getByRole('button', { name: /^copy$/i })).toHaveCount(0)
    }
    const recoverSecret = async () => {
      await combineButton.click()
      await expect(combinePanel.getByRole('heading', { name: /recovered secret|السر المستعاد/i })).toBeVisible()
    }

    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill(shares[1] ?? '')
    await recoverPassphrase.fill(passphrase)

    await recoverSecret()
    await recoverPassphrase.fill('synthetic-edited-recovery-passphrase')
    await expectNoRecoveredSecret()

    await recoverPassphrase.fill(passphrase)
    await recoverSecret()
    await page.evaluate(() => navigator.clipboard.writeText('synthetic-pasted-recovery-passphrase'))
    await recoverPassphrase.press(`${process.platform === 'darwin' ? 'Meta' : 'Control'}+A`)
    await recoverPassphrase.press(`${process.platform === 'darwin' ? 'Meta' : 'Control'}+V`)
    await expect(recoverPassphrase).toHaveValue('synthetic-pasted-recovery-passphrase')
    await expectNoRecoveredSecret()

    await recoverPassphrase.fill(passphrase)
    await recoverSecret()
    await combinePanel.getByRole('button', { name: 'Clear passphrase' }).click()
    await expectNoRecoveredSecret()
  })

  test('exact-match encrypted split and recovery round trip works', async ({ page }) => {
    const secret = 'encrypted-browser-roundtrip-secret'
    const passphrase = 'synthetic-exact-passphrase'
    const shares = await splitAndCollectShares(page, secret, { passphrase })

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill(shares[1] ?? '')
    await page.getByLabel('Passphrase (optional)').fill(passphrase)

    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(page.getByRole('heading', { name: /recovered secret|السر المستعاد/i })).toBeVisible()
    await expect(recoveredSecret(page)).toHaveText(secret)
  })

  test('wrong passphrase fails cleanly', async ({ page }) => {
    const shares = await splitAndCollectShares(page, 'smoke-passphrase-secret', { passphrase: 'correct-passphrase' })

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill(shares[1] ?? '')
    await page.getByLabel('Passphrase (optional)').fill('wrong-passphrase')

    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(page.locator('#combine-panel .alert-error')).toBeVisible()
  })

  test('Split renders the custody rule in English and Arabic', async ({ page }) => {
    await splitAndCollectShares(page, 'synthetic-custody-guidance-secret')
    await expect(page.locator('#split-panel')).toContainText(
      'Keep fewer Recovery shares than the Threshold in every account, device, location, administrator domain, and transport channel.',
    )

    await page.getByRole('button', { name: 'العربية' }).click()
    await expect(page.locator('#split-panel')).toContainText(
      'احتفظ بعدد من حصص الاسترداد أقل من العتبة في كل حساب وجهاز وموقع ونطاق إدارة وقناة نقل.',
    )
  })

  test('Split copies one Recovery share at a time and has no global copy-result action', async ({ page }) => {
    const mod = process.platform === 'darwin' ? 'Meta' : 'Control'
    await instrumentClipboard(page)

    const shares = await splitAndCollectShares(page, 'synthetic-individual-copy-secret')
    await page.keyboard.press(`${mod}+Shift+C`)
    expect(await clipboardWrites(page)).toEqual([])

    await page
      .locator('#split-panel')
      .getByRole('button', { name: 'Copy Recovery share 1' })
      .click()
    await expect.poll(() => clipboardWrites(page)).toEqual([shares[0]])
  })

  test('Combine copy-result shortcut writes the stable recovered Secret', async ({ page }) => {
    const mod = process.platform === 'darwin' ? 'Meta' : 'Control'
    await instrumentClipboard(page)

    const secret = 'synthetic-shortcut-recovered-secret'
    const shares = await splitAndCollectShares(page, secret)
    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill(shares[1] ?? '')
    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(page.getByRole('heading', { name: /recovered secret|السر المستعاد/i })).toBeVisible()

    await page.keyboard.press(`${mod}+Shift+C`)
    await expect.poll(() => clipboardWrites(page)).toEqual([secret])
  })

  test('clipboard failures surface a consistent non-sensitive error', async ({ page }) => {
    const mod = process.platform === 'darwin' ? 'Meta' : 'Control'
    await instrumentClipboard(page, true)
    const shares = await splitAndCollectShares(page, 'synthetic-clipboard-failure-secret')

    const errorRegion = page.locator('[aria-live="assertive"]')
    await page
      .locator('#split-panel')
      .getByRole('button', { name: 'Copy Recovery share 1' })
      .click()
    await expect(errorRegion).toContainText('Could not copy to the clipboard.')
    await expect(errorRegion).not.toContainText('synthetic-clipboard-failure-secret')
    await expect(errorRegion).toBeEmpty({ timeout: 2_000 })

    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    const shareFields = page.locator('#combine-panel textarea')
    await shareFields.nth(0).fill(shares[0] ?? '')
    await shareFields.nth(1).fill(shares[1] ?? '')
    await page.getByRole('button', { name: /^(combine|استعادة)$/i }).click()
    await expect(page.getByRole('heading', { name: /recovered secret|السر المستعاد/i })).toBeVisible()

    await page.keyboard.press(`${mod}+Shift+C`)
    await expect(errorRegion).toContainText('Could not copy to the clipboard.')
    await expect(errorRegion).not.toContainText('synthetic-clipboard-failure-secret')
  })

  test('passphrase controls preserve Arabic labels and RTL layout', async ({ page }) => {
    await page.getByRole('button', { name: 'العربية' }).click()

    const splitPassphrase = page.getByLabel('عبارة مرور (اختياري)')
    await expect(splitPassphrase).toHaveAttribute('type', 'password')
    await expect(page.locator('html')).toHaveAttribute('dir', 'rtl')
    await expect(page.getByRole('button', { name: 'إظهار عبارة المرور' })).toBeVisible()

    await splitPassphrase.fill('عبارة-اختبار')
    await expect(page.getByRole('textbox', { name: 'تأكيد عبارة المرور', exact: true })).toBeVisible()
    await expect(page.getByText('يجب أن تتطابق عبارتا المرور تماماً.')).toBeVisible()

    await page.getByRole('tab', { name: 'استعادة' }).click()
    await expect(page.getByLabel('عبارة مرور (اختياري)')).toHaveAttribute('type', 'password')
    await expect(page.getByRole('textbox', { name: 'تأكيد عبارة المرور', exact: true })).toHaveCount(0)
  })

  test('keyboard shortcuts work for tab switch, help dialog, and submit', async ({ page }) => {
    const mod = process.platform === 'darwin' ? 'Meta' : 'Control'

    await page.keyboard.press('2')
    await expect(page.getByRole('tab', { name: /combine|استعادة/i })).toHaveAttribute('aria-selected', 'true')

    await page.keyboard.press(`${mod}+/`)
    const helpDialog = page.getByRole('dialog', { name: /keyboard shortcuts|اختصارات لوحة المفاتيح/i })
    await expect(helpDialog).toBeVisible()
    await expect(helpDialog).toContainText('Copy recovered Secret (Combine only)')
    await page.keyboard.press('Escape')
    await expect(helpDialog).toHaveCount(0)

    await page.keyboard.press('1')
    await page.locator('#split-panel textarea').first().fill('shortcut-submit-secret')
    await page.keyboard.press(`${mod}+Enter`)
    await expect(page.getByRole('heading', { name: /shares|الحصص/i })).toBeVisible()
  })
})
