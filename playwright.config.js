// @ts-check
const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
  testDir: './tests/e2e',
  timeout: 60_000,
  expect: {
    timeout: 30_000,
  },
  // Run serially because all tests share one wasm page/server lifecycle and input state.
  fullyParallel: false,
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
