// @ts-check
const { defineConfig, devices } = require('@playwright/test');

// Playwright ships its own Chromium, but refuses to install it on some hosts
// (`Playwright does not support chromium on ubuntu26.04-x64`). The nix dev
// shell provides a chromium, so honour an explicit executable when one is
// given and otherwise fall back to Playwright's managed browser. Set
// OG_CHROMIUM_PATH (the dev shell exports it automatically).
const chromiumPath = process.env.OG_CHROMIUM_PATH || undefined;
const chromiumLaunch = (extraArgs = []) => ({
  ...(chromiumPath ? { executablePath: chromiumPath } : {}),
  args: extraArgs,
});

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
        launchOptions: chromiumLaunch(['--use-gl=angle', '--use-angle=swiftshader']),
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
          ...(chromiumPath ? { executablePath: chromiumPath } : {}),
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
    {
      // Keep portrait coverage always-on as a separate Chromium project. The
      // touch suite exercises the same fixed 320x200 menus inside a tall,
      // full-viewport canvas and the portrait-specific control layout.
      name: 'iphone-touch-portrait',
      testMatch: /wasm-touch\.spec\.js/,
      use: {
        ...devices['iPhone 15'],
        browserName: 'chromium',
        launchOptions: {
          ...(chromiumPath ? { executablePath: chromiumPath } : {}),
          args: [
            '--use-gl=angle',
            '--use-angle=swiftshader',
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
          {
            name: 'webkit-iphone-portrait',
            testMatch: /wasm-touch\.spec\.js/,
            use: {
              ...devices['iPhone 15'],
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
