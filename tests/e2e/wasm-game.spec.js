// @ts-check
const { test, expect } = require('@playwright/test');

// Empirically, blank/near-blank 320x200 PNG canvas captures are typically <1.5KB
// in this suite, while real rendered frames are larger. Keep this conservative
// to avoid false negatives from highly compressible scenes.
const MIN_NON_TRIVIAL_PNG_BYTES = 2_000;
const IGNORED_RUNTIME_ERROR_PATTERNS = [/get_asset_path: readlink\(\/proc\/self\/exe\) failed/];

// Helper: wait for the WASM game to finish loading.
// The loading overlay gets class "hidden" when Module.setStatus('') fires.
async function waitForGameLoad(page) {
  const timeoutMs = 60_000;
  const pollMs = 500;
  const diagnosticsEveryMs = 5_000;
  const startTime = Date.now();
  let lastDiagnosticsMs = -diagnosticsEveryMs;
  let latestState = null;

  while (Date.now() - startTime < timeoutMs) {
    latestState = await page.evaluate(() => {
      const loading = document.getElementById('loading');
      const loadingText = document.getElementById('loading-text');
      return {
        exists: Boolean(loading),
        hidden: Boolean(loading && loading.classList.contains('hidden')),
        className: loading ? loading.className : null,
        text: loadingText ? loadingText.textContent : null,
      };
    });

    if (latestState.hidden) {
      await ensureRenderTicker(page);
      await waitForRenderedFrames(page, 3, 5_000);
      return;
    }

    const elapsedMs = Date.now() - startTime;
    if (elapsedMs - lastDiagnosticsMs >= diagnosticsEveryMs) {
      console.log(`[waitForGameLoad] still waiting (${elapsedMs}ms): ${JSON.stringify(latestState)}`);
      lastDiagnosticsMs = elapsedMs;
    }

    await page.waitForTimeout(pollMs);
  }

  throw new Error(
    `Timed out waiting for game load after ${timeoutMs}ms; last #loading state: ${JSON.stringify(latestState)}`,
  );
}

function attachRuntimeErrorCollectors(page, errors) {
  page.on('pageerror', (err) => {
    const message = `pageerror: ${err.message}`;
    if (!shouldIgnoreRuntimeError(message)) {
      errors.push(message);
    }
  });
  page.on('console', (msg) => {
    if (msg.type() === 'error') {
      const message = `console.error: ${msg.text()}`;
      if (!shouldIgnoreRuntimeError(message)) {
        errors.push(message);
      }
    }
  });
}

function shouldIgnoreRuntimeError(message) {
  return IGNORED_RUNTIME_ERROR_PATTERNS.some((pattern) => pattern.test(message));
}

function assertNoRuntimeErrors(errors, context) {
  expect(errors, `Unexpected runtime errors during: ${context}`).toEqual([]);
}

async function ensureRenderTicker(page) {
  await page.evaluate(() => {
    if (window.__pwRenderTickerReady) {
      return;
    }
    window.__pwRenderTickerReady = true;
    window.__pwRenderFrameCount = 0;
    const tick = () => {
      window.__pwRenderFrameCount += 1;
      window.requestAnimationFrame(tick);
    };
    window.requestAnimationFrame(tick);
  });
}

async function waitForRenderedFrames(page, frameDelta = 1, timeoutMs = 5_000) {
  await ensureRenderTicker(page);
  const initialCount = await page.evaluate(() => window.__pwRenderFrameCount || 0);
  await page.waitForFunction(
    ({ start, delta }) => (window.__pwRenderFrameCount || 0) >= start + delta,
    { start: initialCount, delta: frameDelta },
    { timeout: timeoutMs },
  );
}

async function focusCanvas(page) {
  await page.locator('#canvas').click();
  await page.waitForFunction(() => document.activeElement && document.activeElement.id === 'canvas');
}

async function clickCanvasGameCoord(page, gameX, gameY, holdMs = 150) {
  const canvas = page.locator('#canvas');
  const box = await canvas.boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }

  const cssX = box.x + (gameX * box.width) / 320;
  const cssY = box.y + (gameY * box.height) / 200;
  await page.mouse.move(cssX, cssY);
  await page.mouse.down();
  await page.waitForTimeout(holdMs);
  await page.mouse.up();
}

// Helper: capture a snapshot of canvas pixel data via screenshot
// (WebGL canvases don't support getImageData via 2d context)
async function getCanvasScreenshot(page) {
  const canvas = page.locator('#canvas');
  return await canvas.screenshot();
}

// Helper: check if a screenshot buffer has non-trivial content
// (i.e., not a solid black or single-color rectangle)
function hasVisualContent(buffer) {
  return buffer.length > MIN_NON_TRIVIAL_PNG_BYTES;
}

test.describe('Landing Page', () => {
  test('displays title and play button', async ({ page }) => {
    await page.goto('/');

    // Verify the page title
    await expect(page).toHaveTitle('Huddle Dungeon');

    // Verify the heading is visible
    const heading = page.locator('h1');
    await expect(heading).toBeVisible();
    await expect(heading).toHaveText('Huddle Dungeon');

    // Verify the Play button exists and links to play.html
    const playButton = page.locator('a.play-button');
    await expect(playButton).toBeVisible();
    await expect(playButton).toHaveText('Play');
    await expect(playButton).toHaveAttribute('href', 'play.html');
  });
});

test.describe('Game Loading', () => {
  test('WASM module loads and initializes', async ({ page }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.goto('/play.html');

    // Verify canvas element exists
    const canvas = page.locator('#canvas');
    await expect(canvas).toBeVisible();

    // Wait for game to finish loading (Module.setStatus('') hides the overlay)
    await waitForGameLoad(page);
    assertNoRuntimeErrors(errors, 'load completion');

    // Loading overlay should be hidden after initialization
    const loading = page.locator('#loading');
    await expect(loading).toBeHidden();

    // The browser runtime should have invoked the exported web bootstrap exactly once.
    const bootstrapState = await page.evaluate(() => window.__opengladWebBootstrap);
    expect(bootstrapState).toEqual({
      runtimeInitialized: true,
      bootFunctionAvailable: true,
      bootCalls: 1,
    });

    // No runtime errors should have occurred
    assertNoRuntimeErrors(errors, 'post-load assertions');
  });

  test('canvas renders game content', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Take a screenshot of the canvas
    const screenshot = await getCanvasScreenshot(page);

    // The screenshot should have substantial content (not blank/black)
    expect(hasVisualContent(screenshot)).toBe(true);
  });

  test('WebGL context is active', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Verify a WebGL context exists and has a valid drawing buffer.
    // Prefer Emscripten-owned contexts before trying to query the canvas.
    const hasWebGL = await page.evaluate(() => {
      const emscriptenContext = window.Module && (window.Module.ctx || window.Module.GLctx);
      if (emscriptenContext) {
        return emscriptenContext.drawingBufferWidth > 0 && emscriptenContext.drawingBufferHeight > 0;
      }

      const canvas = document.getElementById('canvas');
      if (!canvas) return false;

      const gl =
        canvas.getContext('webgl2') ||
        canvas.getContext('webgl') ||
        canvas.getContext('experimental-webgl');
      return Boolean(gl && gl.drawingBufferWidth > 0 && gl.drawingBufferHeight > 0);
    });

    expect(hasWebGL).toBe(true);
  });
});

test.describe('Game Interaction', () => {
  test('lobby-backed picker start transitions from picker to gameplay', async ({ page }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.addInitScript(() => {
      window.__opengladSeedSinglePlayerTeam = true;
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Skipping the intro avoids the splash sequence, but the picker still
    // needs a few seconds to finish drawing the main menu after the loading
    // overlay disappears.
    await page.waitForTimeout(5_000);
    assertNoRuntimeErrors(errors, 'seeded picker before start input');

    await clickCanvasGameCoord(page, 150, 85);
    await page.waitForTimeout(1_000);
    assertNoRuntimeErrors(errors, 'continue game click');

    await clickCanvasGameCoord(page, 250, 107);
    await page.waitForTimeout(1_000);
    assertNoRuntimeErrors(errors, 'go click');

    await page.waitForFunction(
      () => window.__opengladGameState === 2,
      null,
      { timeout: 15_000 },
    );
    await waitForRenderedFrames(page, 4);
    assertNoRuntimeErrors(errors, 'picker-to-game transition');

    const canvas = page.locator('#canvas');
    await expect(canvas).toBeVisible();
    await expect(page.locator('#loading')).toBeHidden();
  });

  test('lobby-backed picker start uses lobby player count for gameplay views', async ({ page }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.addInitScript(() => {
      window.__opengladSeedPlayerCount = 3;
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);

    await page.waitForTimeout(5_000);
    assertNoRuntimeErrors(errors, 'seeded picker before multi-view start');

    await clickCanvasGameCoord(page, 150, 85);
    await page.waitForTimeout(1_000);
    assertNoRuntimeErrors(errors, 'continue game click');

    await clickCanvasGameCoord(page, 250, 107);
    await page.waitForTimeout(1_000);
    assertNoRuntimeErrors(errors, 'go click');

    await page.waitForFunction(
      () => window.__opengladGameState === 2,
      null,
      { timeout: 15_000 },
    );
    await focusCanvas(page);
    await page.keyboard.press('Escape');
    await page.waitForFunction(() => window.__opengladNumViews === 3, null, {
      timeout: 15_000,
    });
    await waitForRenderedFrames(page, 4);
    assertNoRuntimeErrors(errors, 'picker-to-game multi-view transition');

    await expect(page.locator('#canvas')).toBeVisible();
    await expect(page.locator('#loading')).toBeHidden();
    await expect.poll(async () => page.evaluate(() => window.__opengladNumViews)).toBe(3);
  });

  test('keyboard input does not crash the game', async ({ page }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.goto('/play.html');
    await waitForGameLoad(page);
    assertNoRuntimeErrors(errors, 'initial load before keyboard input');

    // Focus the canvas
    await focusCanvas(page);
    await waitForRenderedFrames(page, 2);
    assertNoRuntimeErrors(errors, 'canvas focus');

    // Send various keyboard inputs
    const keys = ['Enter', 'Escape', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter'];
    for (const key of keys) {
      await page.keyboard.press(key);
      await waitForRenderedFrames(page, 1);
      assertNoRuntimeErrors(errors, `keyboard input: ${key}`);
    }

    await waitForRenderedFrames(page, 8);
    assertNoRuntimeErrors(errors, 'post-input render frames');

    // Canvas should still be visible (game didn't crash)
    const canvas = page.locator('#canvas');
    await expect(canvas).toBeVisible();

    // Loading overlay should still be hidden (no error state)
    const loading = page.locator('#loading');
    await expect(loading).toBeHidden();

    // No runtime errors
    assertNoRuntimeErrors(errors, 'final keyboard interaction assertions');
  });

  test('canvas continues rendering after interaction', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Take initial screenshot
    const before = await getCanvasScreenshot(page);

    // Focus and send input
    await focusCanvas(page);
    await waitForRenderedFrames(page, 2);

    // Press Enter to potentially advance past menu
    await page.keyboard.press('Enter');
    await waitForRenderedFrames(page, 4);

    // Canvas should still have content
    const after = await getCanvasScreenshot(page);
    expect(hasVisualContent(after)).toBe(true);
  });

  test('game navigation changes canvas content', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Take screenshot of initial state (main menu)
    const menuScreenshot = await getCanvasScreenshot(page);

    // Focus canvas and interact
    await focusCanvas(page);
    await waitForRenderedFrames(page, 2);

    // Main menu starts with the first entry focused (Begin/Continue).
    // ArrowDown moves focus to the second entry (Options), and Enter opens it.
    // Expected transition: options/menu overlay redraws the canvas.
    await page.keyboard.press('ArrowDown');
    await waitForRenderedFrames(page, 2);
    await page.keyboard.press('Enter');
    await waitForRenderedFrames(page, 6);

    // Take screenshot after navigation
    const afterScreenshot = await getCanvasScreenshot(page);

    // Both screenshots should have content
    expect(hasVisualContent(menuScreenshot)).toBe(true);
    expect(hasVisualContent(afterScreenshot)).toBe(true);

    // The screenshots should differ (game responded to input)
    // Compare the raw PNG buffers - they should not be identical
    const differ = !menuScreenshot.equals(afterScreenshot);
    expect(differ).toBe(true);
  });
});
