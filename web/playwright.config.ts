import { defineConfig, devices } from '@playwright/test'

const isCi = Boolean(process.env.CI)
const externalBaseUrl = process.env.PLAYWRIGHT_BASE_URL

export default defineConfig({
  testDir: './tests',
  // Retired application tests remain dormant reference source.
  testIgnore: '**/desktop-*.spec.ts',
  fullyParallel: true,
  forbidOnly: isCi,
  retries: isCi ? 2 : 0,
  workers: isCi ? 1 : undefined,
  reporter: 'list',
  use: {
    baseURL: externalBaseUrl || 'http://localhost:5173',
    trace: 'on-first-retry',
  },
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
  webServer: externalBaseUrl
    ? undefined
    : [
        {
          command: 'bun run dev',
          url: 'http://localhost:5173',
          reuseExistingServer: !isCi,
          timeout: 120_000,
        },
        {
          command: 'cd help && bun run dev',
          url: 'http://localhost:4321/help/',
          reuseExistingServer: !isCi,
          timeout: 120_000,
        },
      ],
})
