import { expect, test, type Page } from '@playwright/test'

import { waitForWasmReady } from './a11y-utils'

// Wait past the focus handler's deferred work, not just the focus dispatch.
async function nextFrames(page: Page) {
  await page.evaluate(() => new Promise<void>((resolve) => {
    requestAnimationFrame(() => requestAnimationFrame(() => resolve()))
  }))
}

test.describe('Split touch focus @smoke', () => {
  test.use({ hasTouch: true, viewport: { width: 390, height: 844 } })

  test('leaving a numeric field before the next frame does not steal focus back', async ({ page }) => {
    const errors: string[] = []
    page.on('pageerror', (error) => errors.push(error.message))
    await page.goto('/')
    await waitForWasmReady(page)

    for (const id of ['split-k', 'split-n']) {
      // Both focus events happen in one browser task, before any animation frame can run.
      await page.evaluate((id) => {
        document.getElementById(id)?.focus()
        document.querySelector<HTMLTextAreaElement>('#split-panel textarea')?.focus()
      }, id)
      await nextFrames(page)
      await expect(page.locator('#split-panel textarea')).toBeFocused()
      expect(errors).toEqual([])
    }
  })

  for (const language of ['en', 'ar']) {
    test(`both numeric fields select their current value for replacement without page errors (${language})`, async ({ page }) => {
      const errors: string[] = []
      page.on('pageerror', (error) => errors.push(error.message))
      await page.goto('/')
      await waitForWasmReady(page)
      expect(await page.evaluate(() => matchMedia('(pointer: coarse)').matches)).toBe(true)

      if (language === 'ar') await page.getByRole('button', { name: 'العربية' }).click()
      await expect(page.locator('html')).toHaveAttribute('dir', language === 'ar' ? 'rtl' : 'ltr')

      const fields = language === 'ar'
        ? [['إجمالي الحصص (n)', '4'], ['الحد الأدنى للاستعادة (k)', '3']]
        : [['Total shares to create (n)', '4'], ['Minimum shares to recover (k)', '3']]
      for (const [name, replacement] of fields) {
        const field = page.getByRole('spinbutton', { name, exact: true })
        // Focus without clicking: click selection must not mask broken deferred focus selection.
        await field.focus()
        await nextFrames(page)
        expect(errors).toEqual([])
        // Number inputs do not expose selectionStart/End; typing proves the whole value was selected.
        await page.keyboard.insertText(replacement)
        await expect(field).toHaveValue(replacement)
      }
      expect(errors).toEqual([])
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true)
    })
  }

  test('unmounting before the frame leaves a replacement form alone', async ({ page }) => {
    const errors: string[] = []
    page.on('pageerror', (error) => errors.push(error.message))
    await page.goto('/')
    await waitForWasmReady(page)
    // Freeze the browser clock so tab navigation and remount finish before focus work runs.
    await page.clock.install()
    await page.clock.pauseAt(new Date())

    for (const id of ['split-k', 'split-n']) {
      const original = await page.locator(`#${id}`).elementHandle()
      expect(original).not.toBeNull()
      await page.locator(`#${id}`).focus()
      await page.locator('#combine-tab').dispatchEvent('click')
      await expect(page.locator('#split-panel')).toHaveCount(0)
      expect(await original!.evaluate((input) => input.isConnected)).toBe(false)
      await page.locator('#split-tab').dispatchEvent('click')
      const secret = page.locator('#split-panel textarea')
      await secret.focus()
      await page.clock.runFor(50)
      await expect(secret).toBeFocused()
      await page.keyboard.insertText('synthetic-replacement-form')
      await expect(secret).toHaveValue('synthetic-replacement-form')
      expect(errors).toEqual([])
      await original!.dispose()
    }
  })

  test('replacement and steppers preserve bounds and invalidate Recovery shares', async ({ page }) => {
    const errors: string[] = []
    page.on('pageerror', (error) => errors.push(error.message))
    await page.goto('/')
    await waitForWasmReady(page)
    const panel = page.locator('#split-panel')
    const threshold = panel.getByRole('spinbutton', { name: 'Minimum shares to recover (k)', exact: true })
    const count = panel.getByRole('spinbutton', { name: 'Total shares to create (n)', exact: true })
    const decrease = panel.getByRole('button', { name: 'Decrease', exact: true })
    const increase = panel.getByRole('button', { name: 'Increase', exact: true })
    const shares = panel.getByRole('heading', { name: 'Recovery shares', exact: true })

    async function replace(field: typeof count, value: string) {
      await panel.locator('textarea').focus()
      await field.focus()
      await nextFrames(page)
      await page.keyboard.insertText(value)
    }
    async function generate() {
      await panel.locator('textarea').fill('synthetic-touch-focus-secret')
      await panel.getByRole('button', { name: 'Split', exact: true }).click()
      await expect(shares).toBeVisible()
    }
    async function expectInvalidated() {
      await expect(shares).toHaveCount(0)
      await expect(panel.getByRole('button', { name: /^Copy Recovery share /i })).toHaveCount(0)
      await expect(panel.getByRole('status')).toHaveCount(0)
    }

    await expect(decrease.first()).toBeDisabled()
    await generate()
    await replace(threshold, '3')
    await expect(threshold).toHaveValue('3')
    await expect(increase.first()).toBeDisabled()
    await expectInvalidated()
    await generate()
    await replace(count, '4')
    await expect(count).toHaveValue('4')
    await expectInvalidated()

    for (const button of [increase.first(), decrease.first(), increase.last(), decrease.last()]) {
      await generate()
      await button.click()
      await expectInvalidated()
    }
    await expect(threshold).toHaveValue('3')
    await expect(count).toHaveValue('4')

    await replace(count, '999')
    await expect(count).toHaveValue('255')
    await expect(increase.last()).toBeDisabled()
    await replace(threshold, '999')
    await expect(threshold).toHaveValue('255')
    await expect(increase.first()).toBeDisabled()
    await replace(count, '1')
    await expect(count).toHaveValue('2')
    await expect(threshold).toHaveValue('2')
    await expect(decrease.first()).toBeDisabled()
    await expect(decrease.last()).toBeDisabled()
    await replace(threshold, '1')
    await expect(threshold).toHaveValue('2')
    expect(errors).toEqual([])
  })
})

test('fine-pointer focus keeps native caret behavior @smoke', async ({ page }) => {
  const errors: string[] = []
  page.on('pageerror', (error) => errors.push(error.message))
  await page.goto('/')
  await waitForWasmReady(page)
  expect(await page.evaluate(() => matchMedia('(pointer: fine)').matches)).toBe(true)
  await expect(page.getByRole('button', { name: 'Increase', exact: true })).toHaveCount(0)
  for (const [id, initial, expected] of [['split-n', '3', '34'], ['split-k', '2', '24']]) {
    const field = page.locator(`#${id}`)
    await expect(field).toHaveValue(initial)
    await field.focus()
    await field.press('End')
    await page.locator('#split-panel textarea').focus()
    await field.focus()
    await nextFrames(page)
    await page.keyboard.insertText('4')
    await expect(field).toHaveValue(expected)
  }
  expect(errors).toEqual([])
})
