// @ts-check
const { test, expect } = require('@playwright/test');
const {
  clickCanvasGameCoord,
  ensureRenderTicker,
  focusCanvas,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
  waitForGameplayProgress,
  waitForGameplayRenderSamples,
  waitForRenderedFrames,
} = require('./wasm_helpers');

// Empirically, blank/near-blank 320x200 PNG canvas captures are typically <1.5KB
// in this suite, while real rendered frames are larger. Keep this conservative
// to avoid false negatives from highly compressible scenes.
const MIN_NON_TRIVIAL_PNG_BYTES = 2_000;
const IGNORED_RUNTIME_ERROR_PATTERNS = [/get_asset_path: readlink\(\/proc\/self\/exe\) failed/];

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


// Helper: capture a snapshot of canvas pixel data via screenshot
// (WebGL canvases don't support getImageData via 2d context)
async function getCanvasScreenshot(page) {
  const canvas = page.locator('#canvas');
  return await canvas.screenshot();
}

async function movePointerOffCanvas(page, box) {
  const viewport = page.viewportSize();
  if (!viewport) {
    throw new Error('Viewport size is unavailable');
  }

  const candidates = [
    { x: 1, y: 1 },
    { x: viewport.width - 2, y: 1 },
    { x: 1, y: viewport.height - 2 },
    { x: viewport.width - 2, y: viewport.height - 2 },
  ];
  const point = candidates.find(
    ({ x, y }) =>
      x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height,
  );
  if (!point) {
    throw new Error('No viewport point is available outside the canvas');
  }

  await page.mouse.move(point.x, point.y);
  // Give the SDL picker a poll cycle to clear any hovered button state.
  await page.waitForTimeout(100);
}

// Capture a sub-rectangle addressed on the classic 320x200 reference grid,
// regardless of the canvas's CSS size. In menus this selects exact logical
// pixels; in zoomed gameplay it selects the same fractional world region.
async function getCanvasGameRegionScreenshot(page, gameX, gameY, gameW, gameH) {
  const box = await page.locator('#canvas').boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }
  await movePointerOffCanvas(page, box);
  return await page.screenshot({
    clip: {
      x: box.x + (gameX * box.width) / 320,
      y: box.y + (gameY * box.height) / 200,
      width: (gameW * box.width) / 320,
      height: (gameH * box.height) / 200,
    },
  });
}

async function pressPickerKey(page, key, settlingMs = 150) {
  // A non-zero hold lets the SDL poll loop observe the key before the browser
  // queues its release; an instantaneous CDP press is easy for the blocking
  // picker loop to miss.
  await page.keyboard.press(key, { delay: 75 });
  await page.waitForTimeout(settlingMs);
}

async function openWebDisplayOptions(page) {
  // Emscripten starts forwarding keyboard input after a real canvas click.
  // Use an inert corner rather than focusCanvas(), whose center lands on a
  // player-count button in the multiplayer web menu.
  await clickCanvasGameCoord(page, 10, 10);

  // Picker nav uses player 1's bindings: S/A/W and left Control, not browser
  // arrow keys and Enter. From CONTINUE, five downs and one left reach the
  // OPTIONS wrench in the multiplayer web layout.
  for (let i = 0; i < 5; ++i) {
    await pressPickerKey(page, 's');
  }
  await pressPickerKey(page, 'a');
  await pressPickerKey(page, 'Control', 300);

  // Main OPTIONS starts on BACK; DISPLAY is two rows below it. On web the
  // mode/resolution rows are hidden, so DISPLAY jumps BACK -> overscan -> Zoom.
  await pressPickerKey(page, 's');
  await pressPickerKey(page, 's');
  await pressPickerKey(page, 'Control', 300);
  await pressPickerKey(page, 's');
  await pressPickerKey(page, 's');
}

async function leaveWebDisplayOptions(page) {
  await pressPickerKey(page, 'Escape', 300);
  await pressPickerKey(page, 'Escape', 500);
}

async function restoreDisplayDefaultsDuringGameplay(page, runtimeLogs) {
  // Ctrl+F12 is the shipped restore-defaults shortcut. It live-reapplies the
  // default canvas and filter while gameplay is active.
  await page.keyboard.press('Control+F12', { delay: 75 });
  await expect
    .poll(
      () => runtimeLogs.some((line) => line.includes('Restored default settings')),
      { timeout: 10_000 },
    )
    .toBe(true);
  await waitForGameplayProgress(page, 15_000);

  // game_loop also sees Ctrl+F12 as the debug-obmap toggle. Balance that
  // incidental toggle so the runtime returns to its starting debug state.
  await page.keyboard.press('F12', { delay: 75 });
  await waitForGameplayProgress(page, 15_000);
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
    await expect(page).toHaveTitle('Openglad');

    // Verify the heading is visible
    const heading = page.locator('h1');
    await expect(heading).toBeVisible();
    await expect(heading).toHaveText('Openglad');

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

    // Desktop defaults request fullscreen and a 640x400 remembered window,
    // but the browser deliberately ignores both. Keep the actual WebGL
    // backing on the classic logical size so CSS scaling does not multiply
    // the software-rendering cost.
    const backingSize = await canvas.evaluate((element) => ({
      width: element.width,
      height: element.height,
    }));
    expect(backingSize).toEqual({ width: 320, height: 200 });

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

    await startSeededSinglePlayerFromPicker(page);
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

    await startSeededSinglePlayerFromPicker(page);
    await page.waitForFunction(() => window.__opengladNumViews === 3, null, {
      timeout: 15_000,
    });
    await waitForRenderedFrames(page, 4);
    assertNoRuntimeErrors(errors, 'picker-to-game multi-view transition');

    await expect(page.locator('#canvas')).toBeVisible();
    await expect(page.locator('#loading')).toBeHidden();
    await expect.poll(async () => page.evaluate(() => window.__opengladNumViews)).toBe(3);
  });

  test('web DISPLAY deepest zoom keeps gameplay live', async ({ page }) => {
    test.setTimeout(90_000);

    const errors = [];
    const runtimeLogs = [];
    attachRuntimeErrorCollectors(page, errors);
    page.on('console', (msg) => runtimeLogs.push(msg.text()));

    await page.addInitScript(() => {
      window.__opengladSeedSinglePlayerTeam = true;
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await openWebDisplayOptions(page);

    const defaultZoomLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      107,
      82,
      9,
    );

    // Walk 1.0 -> 0.1. Besides updating the fixed DISPLAY menu, the deepest
    // setting allocates the browser's largest 3200x2000 world canvas.
    for (let i = 0; i < 9; ++i) {
      await pressPickerKey(page, 'Control');
    }
    const deepestZoomLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      107,
      82,
      9,
    );
    expect(deepestZoomLabel.equals(defaultZoomLabel)).toBe(false);

    // Persist 0.1, launch through the normal picker, and prove a real world
    // frame (not merely the fixed menu canvas) is still published and drawn.
    await leaveWebDisplayOptions(page);
    await startSeededSinglePlayerFromPicker(page, { preStartSettlingMs: 1_000 });
    await waitForGameplayProgress(page, 15_000);
	// The reported regression was roughly one rendered frame per second.
	// Require sustained fresh engine samples, not just a single eventual tick.
	await waitForGameplayRenderSamples(page, 20, 5_000);
	await expect.poll(async () => page.evaluate(
	  () => window.__opengladCanvasDiagnostics,
	)).toMatchObject({
	  zoom_steps: 1,
	  world_width: 3200,
	  world_height: 2000,
	  smoothing: 'off',
	  smart_used: false,
	});
    const deepestGameplay = await getCanvasGameRegionScreenshot(
      page,
      24,
      20,
      272,
      160,
    );
    expect(hasVisualContent(deepestGameplay)).toBe(true);
    assertNoRuntimeErrors(errors, '0.1 zoom gameplay');

    await restoreDisplayDefaultsDuringGameplay(page, runtimeLogs);
	await expect.poll(async () => page.evaluate(
	  () => window.__opengladCanvasDiagnostics,
	)).toMatchObject({
	  zoom_steps: 10,
	  world_width: 320,
	  world_height: 200,
	  smoothing: 'off',
	});
    const restoredGameplay = await getCanvasGameRegionScreenshot(
      page,
      24,
      20,
      272,
      160,
    );
    expect(hasVisualContent(restoredGameplay)).toBe(true);
    assertNoRuntimeErrors(errors, 'deepest-zoom default restore');
  });

  test('web DISPLAY SAI smoothing keeps gameplay live', async ({ page }) => {
    test.setTimeout(90_000);

    const errors = [];
    const runtimeLogs = [];
    attachRuntimeErrorCollectors(page, errors);
    page.on('console', (msg) => runtimeLogs.push(msg.text()));

    await page.addInitScript(() => {
      window.__opengladSeedSinglePlayerTeam = true;
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await openWebDisplayOptions(page);

    const defaultZoomLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      107,
      82,
      9,
    );
    await pressPickerKey(page, 's');
    const defaultSmoothingLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      130,
      82,
      9,
    );
    await pressPickerKey(page, 'Control');
    const saiLabel = await getCanvasGameRegionScreenshot(page, 125, 130, 82, 9);
    expect(saiLabel.equals(defaultSmoothingLabel)).toBe(false);

    // Return to Zoom and select 0.5. SAI's software scaler is within its work
    // budget here, so the gameplay assertion covers a real smart-filter pass.
    await pressPickerKey(page, 'w');
    for (let i = 0; i < 5; ++i) {
      await pressPickerKey(page, 'Control');
    }
    const halfZoomLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      107,
      82,
      9,
    );
    expect(halfZoomLabel.equals(defaultZoomLabel)).toBe(false);

    await leaveWebDisplayOptions(page);
    await startSeededSinglePlayerFromPicker(page, { preStartSettlingMs: 1_000 });
    await waitForGameplayProgress(page, 15_000);
	await waitForGameplayRenderSamples(page, 20, 5_000);
	await expect.poll(async () => page.evaluate(
	  () => window.__opengladCanvasDiagnostics,
	)).toMatchObject({
	  zoom_steps: 5,
	  world_width: 640,
	  world_height: 400,
	  smoothing: 'sai',
	  smart_used: true,
	  smart_suppressed: false,
	});
    const saiGameplay = await getCanvasGameRegionScreenshot(page, 24, 20, 272, 160);
    expect(hasVisualContent(saiGameplay)).toBe(true);
    assertNoRuntimeErrors(errors, '0.5 zoom with SAI gameplay');

    await restoreDisplayDefaultsDuringGameplay(page, runtimeLogs);
	await expect.poll(async () => page.evaluate(
	  () => window.__opengladCanvasDiagnostics,
	)).toMatchObject({
	  zoom_steps: 10,
	  world_width: 320,
	  world_height: 200,
	  smoothing: 'off',
	});
    const restoredGameplay = await getCanvasGameRegionScreenshot(
      page,
      24,
      20,
      272,
      160,
    );
    expect(hasVisualContent(restoredGameplay)).toBe(true);
    assertNoRuntimeErrors(errors, 'SAI default restore');
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
