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
const WORLD_CANVAS_PIXEL_BUDGET = 8_388_608;
const SMART_SCALE_SCRATCH_PIXEL_BUDGET = 4_096_000;

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

async function getCanvasSizing(canvas) {
  return await canvas.evaluate((element) => {
    const rect = element.getBoundingClientRect();
    return {
      backingWidth: element.width,
      backingHeight: element.height,
      renderedCssWidth: rect.width,
      renderedCssHeight: rect.height,
      devicePixelRatio: window.devicePixelRatio,
    };
  });
}

function assertCanvasBackingMatchesRenderedCss(size) {
  expect(size.renderedCssWidth).toBeGreaterThan(0);
  expect(size.renderedCssHeight).toBeGreaterThan(0);
  expect(
    Math.abs(size.backingWidth - size.renderedCssWidth * size.devicePixelRatio),
  ).toBeLessThanOrEqual(1);
  expect(
    Math.abs(size.backingHeight - size.renderedCssHeight * size.devicePixelRatio),
  ).toBeLessThanOrEqual(1);
  expect(size.backingWidth / size.backingHeight).toBeCloseTo(
    size.renderedCssWidth / size.renderedCssHeight,
    2,
  );
}

async function waitForCanvasBackingToMatchRenderedCss(canvas) {
  await expect
    .poll(async () => {
      const size = await getCanvasSizing(canvas);
      return (
        Math.abs(
          size.backingWidth - size.renderedCssWidth * size.devicePixelRatio,
        ) <= 1 &&
        Math.abs(
          size.backingHeight - size.renderedCssHeight * size.devicePixelRatio,
        ) <= 1
      );
    })
    .toBe(true);
  const size = await getCanvasSizing(canvas);
  assertCanvasBackingMatchesRenderedCss(size);
  return size;
}

function computeGameplayUiCanvasDims(logicalWidth, logicalHeight) {
  const width = Math.max(320, logicalWidth);
  const height = Math.max(200, logicalHeight);
  if (width * 200 >= height * 320) {
    return {
      width: Math.max(320, Math.floor((width * 200 + Math.floor(height / 2)) / height)),
      height: 200,
    };
  }
  return {
    width: 320,
    height: Math.max(200, Math.floor((height * 320 + Math.floor(width / 2)) / width)),
  };
}

async function assertZoomOneWorldMatchesMasterDensity(page, size) {
  const logicalWidth = Math.round(size.renderedCssWidth);
  const logicalHeight = Math.round(size.renderedCssHeight);
  const expectedUi = computeGameplayUiCanvasDims(logicalWidth, logicalHeight);
  const expected = computeZoomCanvasDims(logicalWidth, logicalHeight, 10);
  await expect
    .poll(async () => {
      const diagnostics = await page.evaluate(
        () => window.__opengladCanvasDiagnostics,
      );
      return Boolean(
        diagnostics &&
          diagnostics.zoom_steps === 10 &&
          diagnostics.world_width === expected.width &&
          diagnostics.world_height === expected.height,
      );
    })
    .toBe(true);
  const latestDiagnostics = await page.evaluate(
    () => window.__opengladCanvasDiagnostics,
  );

  // Zoom 1.0 uses master's classic-density baseline, expanding only the axis
  // needed to match the CSS presentation aspect. Modern backing pixels
  // therefore sharpen presentation without making tiles physically tiny.
  expect(latestDiagnostics.world_width).toBe(expected.width);
  expect(latestDiagnostics.world_height).toBe(expected.height);
  expect(
    latestDiagnostics.world_width / latestDiagnostics.world_height,
  ).toBeCloseTo(size.renderedCssWidth / size.renderedCssHeight, 2);
  expect(latestDiagnostics.gameplay_ui_width).toBe(expectedUi.width);
  expect(latestDiagnostics.gameplay_ui_height).toBe(expectedUi.height);
}

function computeZoomCanvasDims(logicalWidth, logicalHeight, zoomSteps) {
  const steps = Math.max(1, Math.min(10, zoomSteps));
  const baseline = computeGameplayUiCanvasDims(logicalWidth, logicalHeight);
  const scaledWidth = Math.floor(baseline.width * 10 / steps);
  return {
    width: scaledWidth - scaledWidth % 4,
    height: Math.floor(baseline.height * 10 / steps),
  };
}

function deepestSafeZoomSteps(logicalWidth, logicalHeight, maxTextureSize) {
  for (let steps = 1; steps <= 10; ++steps) {
    const dims = computeZoomCanvasDims(logicalWidth, logicalHeight, steps);
    const fitsTexture =
      !maxTextureSize ||
      (dims.width <= maxTextureSize && dims.height <= maxTextureSize);
    if (fitsTexture && dims.width * dims.height <= WORLD_CANVAS_PIXEL_BUDGET) {
      return steps;
    }
  }
  return 10;
}

function deepestSmartZoomSteps(
  logicalWidth,
  logicalHeight,
  minimumZoomSteps,
  maxTextureSize,
) {
  for (let steps = minimumZoomSteps; steps <= 10; ++steps) {
    const dims = computeZoomCanvasDims(logicalWidth, logicalHeight, steps);
    const smartWidth = dims.width * 2;
    const smartHeight = dims.height * 2;
    const fitsTexture =
      !maxTextureSize ||
      (smartWidth <= maxTextureSize && smartHeight <= maxTextureSize);
    if (
      fitsTexture &&
      smartWidth * smartHeight <= SMART_SCALE_SCRATCH_PIXEL_BUDGET
    ) {
      return steps;
    }
  }
  return 10;
}

async function getWebWorldScaleContract(page) {
  const canvas = page.locator('#canvas');
  const size = await waitForCanvasBackingToMatchRenderedCss(canvas);
  const logicalWidth = Math.round(size.renderedCssWidth);
  const logicalHeight = Math.round(size.renderedCssHeight);
  const baselineDims = computeZoomCanvasDims(logicalWidth, logicalHeight, 10);
  await expect
    .poll(() => page.evaluate(() => window.__opengladCanvasDiagnostics))
    .toMatchObject({
      zoom_steps: 10,
      world_width: baselineDims.width,
      world_height: baselineDims.height,
    });

  const maxTextureSize = await page.evaluate(() => {
    const gl = window.Module && (window.Module.GLctx || window.Module.ctx);
    return gl ? gl.getParameter(gl.MAX_TEXTURE_SIZE) : 0;
  });
  const minimumZoomSteps = deepestSafeZoomSteps(
    logicalWidth,
    logicalHeight,
    maxTextureSize,
  );
  const minimumSmartZoomSteps = deepestSmartZoomSteps(
    logicalWidth,
    logicalHeight,
    minimumZoomSteps,
    maxTextureSize,
  );

  return {
    logicalWidth,
    logicalHeight,
    baselineDims,
    maxTextureSize,
    minimumZoomSteps,
    minimumSmartZoomSteps,
  };
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

    // Keep the backing aligned with the shell's fitted 16:10 CSS size. A
    // smaller fixed backing makes the browser upscale and blur every frame.
    const initialSize = await waitForCanvasBackingToMatchRenderedCss(canvas);
    expect(initialSize.renderedCssWidth / initialSize.renderedCssHeight).toBeCloseTo(
      1.6,
      5,
    );
    await assertZoomOneWorldMatchesMasterDensity(page, initialSize);

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

  test('HiDPI recovery keeps physical backing and master-density world sizing', async ({
    browser,
    baseURL,
  }) => {
    const context = await browser.newContext({
      baseURL,
      viewport: { width: 800, height: 600 },
      deviceScaleFactor: 2,
    });
    const page = await context.newPage();
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    try {
      await page.goto('/play.html');
      await waitForGameLoad(page);

      const canvas = page.locator('#canvas');
      const initialSize = await waitForCanvasBackingToMatchRenderedCss(canvas);
      expect(initialSize.devicePixelRatio).toBe(2);
      expect(
        initialSize.renderedCssWidth / initialSize.renderedCssHeight,
      ).toBeCloseTo(1.6, 5);
      await assertZoomOneWorldMatchesMasterDensity(page, initialSize);

      await canvas.evaluate((element) => {
        element.width = 1272;
        element.height = 573;
        window.dispatchEvent(new Event('resize'));
      });
      const recoveredSize = await waitForCanvasBackingToMatchRenderedCss(canvas);
      expect(recoveredSize.backingWidth).toBe(
        Math.round(recoveredSize.renderedCssWidth * 2),
      );
      expect(recoveredSize.backingHeight).toBe(
        Math.round(recoveredSize.renderedCssHeight * 2),
      );
      await assertZoomOneWorldMatchesMasterDensity(page, recoveredSize);
      assertNoRuntimeErrors(errors, 'HiDPI canvas recovery');
    } finally {
      await context.close();
    }
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

  test('post-boot resize recovery restores the CSS-fitted canvas backing', async ({ page }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.goto('/play.html');
    await waitForGameLoad(page);

    const restoreExportAvailable = await page.evaluate(() => {
      const canvas = document.getElementById('canvas');
      if (!canvas) {
        throw new Error('Canvas is unavailable');
      }

      const available =
        typeof Module._openglad_web_restore_canvas_backing === 'function';

      // Model SDL3's fullscreen-exit race deterministically. The post-boot
      // listener must run after the other resize listeners and restore both
      // the fitted backing and the CSS presentation size in this same event.
      canvas.width = 1272;
      canvas.height = 573;
      window.dispatchEvent(new Event('resize'));
      return available;
    });

    expect(restoreExportAvailable).toBe(true);
    const recoveredSize = await waitForCanvasBackingToMatchRenderedCss(
      page.locator('#canvas'),
    );
    expect(
      recoveredSize.renderedCssWidth / recoveredSize.renderedCssHeight,
    ).toBeCloseTo(1.6, 5);
    await assertZoomOneWorldMatchesMasterDensity(page, recoveredSize);

    await waitForRenderedFrames(page, 4);
    expect(hasVisualContent(await getCanvasScreenshot(page))).toBe(true);
    assertNoRuntimeErrors(errors, 'post-boot canvas resize recovery');
  });

  test('fullscreen control enters and exit restores backing and focus', async ({
    page,
  }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.addInitScript(() => {
      window.__opengladKeyboardLockTest = {
        lockCalls: [],
        unlockCalls: 0,
      };
      Object.defineProperty(navigator, 'keyboard', {
        configurable: true,
        value: {
          lock: async (keys) => {
            window.__opengladKeyboardLockTest.lockCalls.push([...keys]);
          },
          unlock: () => {
            window.__opengladKeyboardLockTest.unlockCalls += 1;
          },
        },
      });
    });

    await page.goto('/play.html');
    await waitForGameLoad(page);

    const canvas = page.locator('#canvas');
    const fullscreenButton = page.getByRole('button', {
      name: 'Enter fullscreen',
    });
    const fullscreenStatus = page.getByRole('status');
    await expect(fullscreenButton).toBeVisible();
    await expect(fullscreenButton).toHaveAttribute('title', 'Enter fullscreen');
    await expect(fullscreenButton).toHaveAttribute('aria-controls', 'canvas');
    await expect(fullscreenButton).toHaveCSS('width', '44px');
    await expect(fullscreenButton).toHaveCSS('height', '44px');
    await expect(fullscreenStatus).toBeEmpty();

    const preFullscreenSize = await waitForCanvasBackingToMatchRenderedCss(canvas);
    expect(
      preFullscreenSize.renderedCssWidth / preFullscreenSize.renderedCssHeight,
    ).toBeCloseTo(1.6, 5);
    await assertZoomOneWorldMatchesMasterDensity(page, preFullscreenSize);

    // Playwright's real click follows the same trusted user-gesture path as
    // the shipped control; production code must not enter fullscreen at boot.
    await fullscreenButton.click();
    await page.waitForFunction(
      () => document.fullscreenElement?.id === 'canvas',
    );
    await expect(fullscreenButton).toBeHidden();
    await expect
      .poll(() => page.evaluate(() => document.activeElement?.id))
      .toBe('canvas');
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.lockCalls),
      )
      .toEqual([['Escape']]);
    await expect(fullscreenStatus).toContainText(
      'Escape controls the game; press and hold Escape to leave fullscreen',
    );

    const fullscreenSize = await waitForCanvasBackingToMatchRenderedCss(canvas);
    await assertZoomOneWorldMatchesMasterDensity(page, fullscreenSize);

    // Headless Chromium does not route CDP-generated Escape through browser
    // chrome, so use the Fullscreen API to exercise the same exit events that
    // the browser-reserved physical Escape emits in a real window.
    await page.evaluate(() => document.exitFullscreen());
    await page.waitForFunction(() => document.fullscreenElement === null);
    await expect(fullscreenButton).toBeVisible();
    await expect(fullscreenStatus).toBeEmpty();
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.unlockCalls),
      )
      .toBe(1);

    await expect
      .poll(() =>
        canvas.evaluate((element) => ({
          backingWidth: element.width,
          backingHeight: element.height,
          focused: document.activeElement === element,
        })),
      )
      .toMatchObject({
        backingWidth: preFullscreenSize.backingWidth,
        backingHeight: preFullscreenSize.backingHeight,
        focused: true,
      });

    const exitSize = await getCanvasSizing(canvas);
    assertCanvasBackingMatchesRenderedCss(exitSize);
    expect(exitSize.renderedCssWidth / exitSize.renderedCssHeight).toBeCloseTo(1.6, 5);
    await assertZoomOneWorldMatchesMasterDensity(page, exitSize);

    await waitForRenderedFrames(page, 4);
    expect(hasVisualContent(await getCanvasScreenshot(page))).toBe(true);

    // A denied Keyboard Lock request must not undo a successful fullscreen
    // transition. The browser-reserved Escape behavior remains the fallback.
    await page.evaluate(() => {
      navigator.keyboard.lock = async () => {
        throw new DOMException('blocked for test', 'NotAllowedError');
      };
    });
    await fullscreenButton.click();
    await page.waitForFunction(
      () => document.fullscreenElement?.id === 'canvas',
    );
    await expect(fullscreenStatus).toContainText(
      'Escape leaves fullscreen; press it again to control the game',
    );
    await expect(canvas).toBeFocused();
    await page.evaluate(() => document.exitFullscreen());
    await page.waitForFunction(() => document.fullscreenElement === null);
    await expect(fullscreenStatus).toBeEmpty();
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.unlockCalls),
      )
      .toBe(2);

    // A browser or embedding policy can deny a valid user request. Keep that
    // failure perceivable to keyboard and screen-reader users instead of
    // leaving a silently dead-looking icon.
    await canvas.evaluate((element) => {
      Object.defineProperty(element, 'requestFullscreen', {
        configurable: true,
        value: () => Promise.reject(new Error('blocked for test')),
      });
    });
    await fullscreenButton.click();
    await expect(fullscreenStatus).toContainText(
      'Fullscreen could not be opened',
    );
    await expect(fullscreenButton).toBeFocused();
    assertNoRuntimeErrors(errors, 'DOM fullscreen exit recovery');
  });

  test('late Keyboard Lock completion cannot unlock a newer fullscreen session', async ({
    page,
  }) => {
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await page.addInitScript(() => {
      window.__opengladKeyboardLockTest = {
        pending: [],
        unlockCalls: 0,
      };
      Object.defineProperty(navigator, 'keyboard', {
        configurable: true,
        value: {
          lock: (keys) =>
            new Promise((resolve) => {
              window.__opengladKeyboardLockTest.pending.push({
                keys: [...keys],
                resolve,
              });
            }),
          unlock: () => {
            window.__opengladKeyboardLockTest.unlockCalls += 1;
          },
        },
      });
    });

    await page.goto('/play.html');
    await waitForGameLoad(page);

    const canvas = page.locator('#canvas');
    const fullscreenButton = page.getByRole('button', {
      name: 'Enter fullscreen',
    });
    const fullscreenStatus = page.getByRole('status');

    await fullscreenButton.click();
    await page.waitForFunction(
      () => document.fullscreenElement?.id === 'canvas',
    );
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.pending.length),
      )
      .toBe(1);

    await page.evaluate(() => document.exitFullscreen());
    await page.waitForFunction(() => document.fullscreenElement === null);
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.unlockCalls),
      )
      .toBe(1);

    await fullscreenButton.click();
    await page.waitForFunction(
      () => document.fullscreenElement?.id === 'canvas',
    );
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.pending.length),
      )
      .toBe(2);

    // Complete the first session's request only after the second fullscreen
    // session owns the current request. It must not globally unlock or
    // invalidate that newer request.
    await page.evaluate(async () => {
      window.__opengladKeyboardLockTest.pending[0].resolve();
      await new Promise((resolve) => setTimeout(resolve, 0));
    });
    expect(
      await page.evaluate(() => window.__opengladKeyboardLockTest.unlockCalls),
    ).toBe(1);

    await page.evaluate(async () => {
      window.__opengladKeyboardLockTest.pending[1].resolve();
      await new Promise((resolve) => setTimeout(resolve, 0));
    });
    await expect(fullscreenStatus).toContainText(
      'Escape controls the game; press and hold Escape to leave fullscreen',
    );
    expect(
      await page.evaluate(() => window.__opengladKeyboardLockTest.unlockCalls),
    ).toBe(1);
    await expect(canvas).toBeFocused();

    await page.evaluate(() => document.exitFullscreen());
    await page.waitForFunction(() => document.fullscreenElement === null);
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladKeyboardLockTest.unlockCalls),
      )
      .toBe(2);
    assertNoRuntimeErrors(errors, 'stale Keyboard Lock completion');
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
    const scaleContract = await getWebWorldScaleContract(page);
    await openWebDisplayOptions(page);

    const defaultZoomLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      107,
      82,
      9,
    );

    // Walk 1.0 to the deepest step that fits this browser's CSS-logical
    // window, world-surface budget, and renderer texture limit.
    for (let i = 0; i < 10 - scaleContract.minimumZoomSteps; ++i) {
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

    const deepestDims = computeZoomCanvasDims(
      scaleContract.logicalWidth,
      scaleContract.logicalHeight,
      scaleContract.minimumZoomSteps,
    );

    // Persist the resource-safe deepest step, launch through the normal
    // picker, and prove a real world frame is still published and drawn.
    await leaveWebDisplayOptions(page);
    await startSeededSinglePlayerFromPicker(page, { preStartSettlingMs: 1_000 });
    await waitForGameplayProgress(page, 15_000);
    // The reported regression was roughly one rendered frame per second.
    // Require sustained fresh engine samples, not just a single eventual tick.
    await waitForGameplayRenderSamples(page, 20, 5_000);
    await expect.poll(async () => page.evaluate(
      () => window.__opengladCanvasDiagnostics,
    )).toMatchObject({
      zoom_steps: scaleContract.minimumZoomSteps,
      world_width: deepestDims.width,
      world_height: deepestDims.height,
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
    assertNoRuntimeErrors(errors, 'resource-safe deepest zoom gameplay');

    await restoreDisplayDefaultsDuringGameplay(page, runtimeLogs);
    await expect.poll(async () => page.evaluate(
      () => window.__opengladCanvasDiagnostics,
    )).toMatchObject({
      zoom_steps: 10,
      world_width: scaleContract.baselineDims.width,
      world_height: scaleContract.baselineDims.height,
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
    assertNoRuntimeErrors(errors, 'resource-safe deepest-zoom default restore');
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
    const scaleContract = await getWebWorldScaleContract(page);
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

    // Return to Zoom and select the deepest step whose doubled SAI scratch
    // fits both the CPU work budget and the renderer texture limit.
    await pressPickerKey(page, 'w');
    for (let i = 0; i < 10 - scaleContract.minimumSmartZoomSteps; ++i) {
      await pressPickerKey(page, 'Control');
    }
    const smartZoomLabel = await getCanvasGameRegionScreenshot(
      page,
      125,
      107,
      82,
      9,
    );
    expect(smartZoomLabel.equals(defaultZoomLabel)).toBe(false);

    const smartDims = computeZoomCanvasDims(
      scaleContract.logicalWidth,
      scaleContract.logicalHeight,
      scaleContract.minimumSmartZoomSteps,
    );

    await leaveWebDisplayOptions(page);
    await startSeededSinglePlayerFromPicker(page, { preStartSettlingMs: 1_000 });
    await waitForGameplayProgress(page, 15_000);
    await waitForGameplayRenderSamples(page, 20, 5_000);
    await expect.poll(async () => page.evaluate(
      () => window.__opengladCanvasDiagnostics,
    )).toMatchObject({
      zoom_steps: scaleContract.minimumSmartZoomSteps,
      world_width: smartDims.width,
      world_height: smartDims.height,
      smoothing: 'sai',
      smart_used: true,
      smart_suppressed: false,
    });
    const saiGameplay = await getCanvasGameRegionScreenshot(page, 24, 20, 272, 160);
    expect(hasVisualContent(saiGameplay)).toBe(true);
    assertNoRuntimeErrors(errors, 'resource-safe zoom with SAI gameplay');

    await restoreDisplayDefaultsDuringGameplay(page, runtimeLogs);
    await expect.poll(async () => page.evaluate(
      () => window.__opengladCanvasDiagnostics,
    )).toMatchObject({
      zoom_steps: 10,
      world_width: scaleContract.baselineDims.width,
      world_height: scaleContract.baselineDims.height,
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
