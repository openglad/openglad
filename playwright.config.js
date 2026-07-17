// @ts-check
const { defineConfig, devices } = require('@playwright/test');

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
      testIgnore: /wasm-touch\.spec\.js/,
      use: {
        browserName: 'chromium',
        // WebGL support in headless Chromium
        launchOptions: {
          args: ['--use-gl=angle', '--use-angle=swiftshader'],
        },
      },
    },
    {
      // iPhone emulation on the chromium engine: keeps hasTouch/isMobile/
      // DPR 3/iOS UA from the device descriptor while exercising the same
      // Pointer Events path SDL3's emscripten driver consumes on iOS Safari.
      // True WebKit runs are gated behind OG_WEBKIT below (this dev box
      // cannot satisfy the webkit build's library deps; CI's ubuntu-24.04
      // image can via `npx playwright install --with-deps webkit`).
      name: 'iphone-touch',
      testMatch: /wasm-touch\.spec\.js/,
      use: {
        ...devices['iPhone 15 landscape'],
        browserName: 'chromium',
        launchOptions: {
          args: [
            '--use-gl=angle',
            '--use-angle=swiftshader',
            // Keeps the audio-unlock assertion meaningful: the context must
            // boot suspended until a real gesture resumes it.
            '--autoplay-policy=user-gesture-required',
          ],
        },
      },
    },
    ...(process.env.OG_WEBKIT
      ? [
          {
            name: 'webkit-iphone',
            testMatch: /wasm-touch\.spec\.js/,
            use: {
              ...devices['iPhone 15 landscape'],
            },
          },
        ]
      : []),
  ],
  webServer: {
    // Serve the prebuilt WASM artifacts in dist/ during Playwright runs.
    command: 'npx http-server dist -p 8089 -s',
    port: 8089,
    reuseExistingServer: !process.env.CI,
    timeout: 10_000,
  },
});
