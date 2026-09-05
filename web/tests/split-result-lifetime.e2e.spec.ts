import { expect, test, type Browser, type Page } from '@playwright/test'

import { waitForWasmReady } from './a11y-utils'

const splitPanel = (page: Page) => page.locator('#split-panel')
const recoverySharesHeading = (page: Page) =>
  splitPanel(page).getByRole('heading', { name: /^(recovery shares|حصص الاسترداد)$/i })

async function generateRecoveryShares(page: Page, secret = 'synthetic-split-lifetime-secret') {
  await splitPanel(page).locator('textarea').fill(secret)
  await splitPanel(page).getByRole('button', { name: /^(split|قسم)$/i }).click()
  await expect(recoverySharesHeading(page)).toBeVisible()
}

async function expectRecoverySharesInvalidated(page: Page) {
  await expect(recoverySharesHeading(page)).toHaveCount(0)
  await expect(
    splitPanel(page).getByRole('button', {
      name: /^(copy recovery share|نسخ حصة الاسترداد) \d+$/i,
    }),
  ).toHaveCount(0)
  await expect(splitPanel(page).getByRole('status')).toHaveCount(0)
}

async function openCoarsePointerPage(browser: Browser): Promise<{ page: Page; close: () => Promise<void> }> {
  const context = await browser.newContext({
    hasTouch: true,
    viewport: { width: 390, height: 844 },
  })
  const page = await context.newPage()
  await page.goto('/')
  await waitForWasmReady(page)
  return { page, close: () => context.close() }
}

test.describe('Split result lifetime @smoke', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/')
    await waitForWasmReady(page)
  })

  test('changing each Split input invalidates generated Recovery shares', async ({ page }) => {
    const cases: Array<{ name: string; change: () => Promise<void> }> = [
      {
        name: 'Secret',
        change: () => splitPanel(page).locator('textarea').fill('changed-synthetic-secret'),
      },
      {
        name: 'Threshold',
        change: () => splitPanel(page).locator('#split-k').fill('3'),
      },
      {
        name: 'Share count',
        change: () => splitPanel(page).locator('#split-n').fill('4'),
      },
      {
        name: 'Share encoding',
        change: () => splitPanel(page).getByText('Letters', { exact: true }).click(),
      },
      {
        name: 'Passphrase protection',
        change: () => splitPanel(page).locator('#split-passphrase').fill('synthetic-passphrase'),
      },
    ]

    for (const dependency of cases) {
      await test.step(dependency.name, async () => {
        await generateRecoveryShares(page)
        await dependency.change()
        await expectRecoverySharesInvalidated(page)
      })
    }
  })

  test('clear and clipboard paste paths invalidate generated Recovery shares', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    await page.reload()
    await waitForWasmReady(page)

    await generateRecoveryShares(page)
    await splitPanel(page).getByRole('button', { name: /clear secret/i }).click()
    await expectRecoverySharesInvalidated(page)

    await generateRecoveryShares(page, 'synthetic-secret-for-paste-path')
    await page.evaluate(() => navigator.clipboard.writeText('synthetic-pasted-passphrase'))
    await splitPanel(page).getByRole('button', { name: /paste passphrase/i }).click()
    await expectRecoverySharesInvalidated(page)
  })

  test('invalidated Recovery shares are unavailable to the global copy shortcut', async ({ page, context }) => {
    await context.grantPermissions(['clipboard-read', 'clipboard-write'])
    const modifier = process.platform === 'darwin' ? 'Meta' : 'Control'

    await generateRecoveryShares(page)
    await splitPanel(page).locator('textarea').fill('changed-before-copy')
    await page.evaluate(() => navigator.clipboard.writeText('clipboard-sentinel'))
    await page.keyboard.press(`${modifier}+Shift+C`)

    await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe('clipboard-sentinel')
  })

  test('announces only the number of generated Recovery shares in English and Arabic', async ({ page }) => {
    const secret = 'synthetic-live-region-secret'
    await generateRecoveryShares(page, secret)

    const englishStatus = splitPanel(page).getByRole('status')
    await expect(englishStatus).toHaveText('3 Recovery shares ready.')
    await expect(englishStatus).not.toContainText(secret)
    const firstShare = await splitPanel(page).locator('div[dir="ltr"].input').first().textContent()
    expect(firstShare).toBeTruthy()
    await expect(englishStatus).not.toContainText(firstShare ?? 'unexpected-empty-share')

    await page.getByRole('button', { name: 'العربية' }).click()
    await splitPanel(page).locator('#split-n').fill('4')
    await generateRecoveryShares(page, 'synthetic-arabic-status-secret')
    await expect(splitPanel(page).getByRole('status')).toHaveText('4 حصص استرداد جاهزة.')
  })
})

test.describe('Split pending work @smoke', () => {
  test('an input changed during WASM initialization rejects the late result', async ({ page }) => {
    await page.addInitScript(() => {
      Object.defineProperty(window, 'requestIdleCallback', {
        configurable: true,
        value: () => 1,
      })
    })

    let releaseWasm: (() => void) | undefined
    let markWasmStarted: (() => void) | undefined
    const wasmStarted = new Promise<void>((resolve) => {
      markWasmStarted = resolve
    })
    const wasmGate = new Promise<void>((resolve) => {
      releaseWasm = resolve
    })

    await page.route('**/*.wasm', async (route) => {
      markWasmStarted?.()
      await wasmGate
      await route.continue()
    })

    await page.goto('/')
    await waitForWasmReady(page)
    await splitPanel(page).locator('textarea').fill('synthetic-pending-secret')
    await splitPanel(page).getByRole('button', { name: /^(split|قسم)$/i }).click()
    await wasmStarted

    await splitPanel(page).locator('textarea').fill('changed-while-wasm-pending')
    const wasmResponse = page.waitForResponse((response) => response.url().endsWith('.wasm'))
    releaseWasm?.()
    await wasmResponse
    await page.waitForTimeout(300)

    await expectRecoverySharesInvalidated(page)
    await expect(splitPanel(page).getByRole('button', { name: /^(split|قسم)$/i })).toBeEnabled()
  })
})

test.describe('Split coarse-pointer controls @smoke', () => {
  test('a numeric stepper change invalidates generated Recovery shares', async ({ browser }) => {
    const coarsePointer = await openCoarsePointerPage(browser)
    try {
      await generateRecoveryShares(coarsePointer.page)
      const increaseButtons = splitPanel(coarsePointer.page).getByRole('button', { name: /increase/i })
      await expect(increaseButtons).toHaveCount(2)
      await increaseButtons.first().click()
      await expectRecoverySharesInvalidated(coarsePointer.page)
    } finally {
      await coarsePointer.close()
    }
  })
})
