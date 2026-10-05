import AxeBuilder from '@axe-core/playwright'
import { expect, type Page } from '@playwright/test'

/**
 * Accessibility test utilities using axe-core.
 * 
 * WCAG 2.2 AA compliance requires 0 serious and 0 critical violations.
 */

export interface A11yResult {
  violations: Array<{
    id: string
    impact: 'minor' | 'moderate' | 'serious' | 'critical'
    description: string
    help: string
    helpUrl: string
    nodes: Array<{
      html: string
      target: string[]
      failureSummary: string
    }>
  }>
}

/**
 * Close Vite's error overlay if present.
 * The overlay can block interactions and cause a11y violations.
 */
export async function closeViteErrorOverlay(page: Page): Promise<void> {
  try {
    // Try pressing Escape to dismiss the overlay
    await page.keyboard.press('Escape')
    await page.waitForTimeout(500)
  } catch {
    // No error overlay present, that's fine
  }
}

/**
 * Run axe accessibility scan on a page.
 * Fails the test if serious or critical violations are found.
 */
export async function expectNoA11yViolations(page: Page): Promise<void> {
  // Close Vite error overlay if present
  await closeViteErrorOverlay(page)

  const accessibilityScanResults = await new AxeBuilder({ page })
    .withTags(['wcag2a', 'wcag2aa', 'wcag21aa', 'wcag22aa'])
    .exclude('vite-error-overlay') // Exclude Vite's error overlay
    .exclude('[part="stack"]') // Exclude stack trace elements
    .analyze()

  const seriousAndCritical = accessibilityScanResults.violations.filter(
    (violation) => violation.impact === 'serious' || violation.impact === 'critical',
  )

  // Log all violations for debugging
  if (accessibilityScanResults.violations.length > 0) {
    console.log('Accessibility violations found:')
    for (const violation of accessibilityScanResults.violations) {
      console.log(`  [${violation.impact}] ${violation.id}: ${violation.description}`)
      for (const node of violation.nodes.slice(0, 3)) {
        console.log(`    - ${node.target.join(', ')}`)
      }
    }
  }

  // Assert no serious or critical violations
  expect(
    seriousAndCritical,
    `Found ${seriousAndCritical.length} serious/critical accessibility violations`,
  ).toHaveLength(0)

  // Also assert no violations at all (ideal state)
  expect(
    accessibilityScanResults.violations,
    `Found ${accessibilityScanResults.violations.length} total accessibility violations`,
  ).toHaveLength(0)
}

/**
 * Wait for the WASM module to be ready.
 * The web app shows a loading state until WASM is initialized.
 */
export async function waitForWasmReady(page: Page): Promise<void> {
  await expect(page.getByRole('tab', { name: /split|تقسيم/i })).toBeVisible()
  await page.waitForFunction(
    async () => {
      try {
        const dynamicImport = new Function('path', 'return import(path)') as (
          path: string,
        ) => Promise<unknown>
        await dynamicImport('/src/wasm_pkg/safeparts_wasm.js')
        return true
      } catch {
        return false
      }
    },
    { timeout: 30000 },
  )
}

export async function openSplitOptions(page: Page) {
  const disclosure = page.getByTestId('split-advanced-options')
  if (!(await disclosure.evaluate((element) => (element as HTMLDetailsElement).open))) {
    await disclosure.locator('summary').click()
  }
}
