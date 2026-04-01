// @ts-check
const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
  testDir: './tests/e2e',
  timeout: 60_000,
  expect: {
    timeout: 30_000,
  },
  // Keep the entire suite on one worker because the seeded WASM tests share
  // the same server lifecycle and the jitter capture depends on uninterrupted
  // motion sampling.
  fullyParallel: false,
  workers: 1,
  retries: 1,
  reporter: [['html', { open: 'never' }], ['list']],
  use: {
    baseURL: 'http://localhost:8089',
    screenshot: 'only-on-failure',
    trace: 'retain-on-failure',
  },
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
        // WebGL support in headless Chromium
        launchOptions: {
          args: ['--use-gl=angle', '--use-angle=swiftshader'],
        },
      },
    },
  ],
  webServer: {
    // Serve the prebuilt WASM artifacts in dist/ during Playwright runs.
    command: 'npx http-server dist -p 8089 -s',
    port: 8089,
    reuseExistingServer: !process.env.CI,
    timeout: 10_000,
  },
});
