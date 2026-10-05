import { expect, test } from '@playwright/test'
import { expectNoA11yViolations, waitForWasmReady } from './a11y-utils'

test.describe('Web App Accessibility @smoke', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/')
    await waitForWasmReady(page)
  })

  test('Split screen has no accessibility violations', async ({ page }) => {
    // Ensure we're on the Split tab
    await expect(page.getByRole('tab', { name: /split|تقسيم/i })).toHaveAttribute('aria-selected', 'true')
    
    await expect(page.locator('#split-panel details')).toHaveCount(1)
    await expect(page.getByTestId('split-advanced-options')).not.toHaveAttribute('open')
    await expect(page.getByLabel('Passphrase (optional)')).not.toBeVisible()
    await expectNoA11yViolations(page)
  })

  test('Combine screen has no accessibility violations', async ({ page }) => {
    // Switch to Combine tab
    await page.getByRole('tab', { name: /combine|استعادة/i }).click()
    
    await expectNoA11yViolations(page)
  })

  test('Split passphrase confirmation has no accessibility violations', async ({ page }) => {
    await page.getByTestId('split-advanced-options').locator('summary').click()
    await page.getByLabel('Passphrase (optional)').fill('synthetic-accessibility-passphrase')
    await expect(page.getByLabel('Confirm passphrase')).toBeVisible()

    await expectNoA11yViolations(page)
  })

  test('Language toggle is accessible', async ({ page }) => {
    // Test both language toggle buttons
    const enButton = page.getByRole('button', { name: 'English' })
    const arButton = page.getByRole('button', { name: 'العربية' })
    
    // Both should be keyboard focusable
    await enButton.focus()
    await expect(enButton).toBeFocused()
    
    await arButton.focus()
    await expect(arButton).toBeFocused()
    
    // Both should have aria-pressed to indicate state
    await expect(enButton).toHaveAttribute('aria-pressed')
    await expect(arButton).toHaveAttribute('aria-pressed')
    
    // Exactly one should be pressed
    const enPressed = await enButton.getAttribute('aria-pressed')
    const arPressed = await arButton.getAttribute('aria-pressed')
    expect([enPressed, arPressed].filter((pressed) => pressed === 'true')).toHaveLength(1)
  })

  test('Tab navigation follows logical order', async ({ page }) => {
    // Get all focusable elements
    const focusableElements = await page.locator('button, a, input, textarea, select, [tabindex]:not([tabindex="-1"])').all()
    
    // Press Tab and verify focus moves through elements
    for (let i = 0; i < Math.min(focusableElements.length, 5); i++) {
      await page.keyboard.press('Tab')
      const focusedElement = await page.evaluate(() => document.activeElement?.tagName)
      expect(focusedElement).not.toBe('BODY')
    }
  })
})
