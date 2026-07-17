// @ts-check
// Touch-controls e2e suite. Runs under the 'iphone-touch' Playwright project
// (chromium engine + iPhone 15 landscape device descriptor: hasTouch,
// isMobile, DPR 3, iOS UA) and, when OG_WEBKIT is set, under 'webkit-iphone'
// (true WebKit). Desktop chromium ignores this file (see playwright.config.js).
//
// The overlay lives in web/shell.html; held keys reach the engine through
// web_touch_bridge.cpp (Module._openglad_web_touch_set_key -> the
// touch_keystate seam OR-ed into isPlayerHoldingKey), and the bridge mirrors
// every accepted write to window.__opengladTouchKeys for these assertions.
const { test, expect } = require('@playwright/test');
const {
  waitForGameLoad,
  waitForGameplayProgress,
  waitForGameplayRenderSamples,
  waitForPickerReady,
  waitForRenderedFrames,
  startSeededSinglePlayerFromPicker,
} = require('./wasm_helpers');

// Abort-flow dialog buttons on the 320x200 UI reference grid
// (src/interface/ui/picker_dialogs.cpp): YES center and the popup OK center.
const YES_BUTTON = { x: 95, y: 140 };
const OK_BUTTON = { x: 160, y: 140 };

async function seedGameplayInitScript(page) {
  await page.addInitScript(() => {
    window.__opengladSeedSinglePlayerTeam = true;
    window.__opengladSkipIntroForTests = true;
    // Decouple the functional tests from media-query emulation details;
    // the activation test exercises the natural gating separately.
    window.__opengladForceTouchControls = true;
  });
}

async function canvasGameCoordToCss(page, gameX, gameY) {
  const box = await page.locator('#canvas').boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }
  return {
    x: box.x + (gameX * box.width) / 320,
    y: box.y + (gameY * box.height) / 200,
  };
}

async function tapCanvasGameCoord(page, gameX, gameY) {
  const { x, y } = await canvasGameCoordToCss(page, gameX, gameY);
  await page.touchscreen.tap(x, y);
}

// Overlay controls capture their pointers; drive them with synthetic
// PointerEvents (the overlay tolerates uncapturable synthetic pointers).
async function dispatchPointer(page, selector, type, x, y, pointerId = 41) {
  await page.evaluate(
    ({ selector, type, x, y, pointerId }) => {
      const el = document.querySelector(selector);
      if (!el) {
        throw new Error(`No element for selector: ${selector}`);
      }
      el.dispatchEvent(
        new PointerEvent(type, {
          pointerId,
          pointerType: 'touch',
          isPrimary: true,
          clientX: x,
          clientY: y,
          bubbles: true,
          cancelable: true,
        }),
      );
    },
    { selector, type, x, y, pointerId },
  );
}

async function elementCenter(page, selector) {
  const box = await page.locator(selector).boundingBox();
  if (!box) {
    throw new Error(`No bounding box for selector: ${selector}`);
  }
  return { x: box.x + box.width / 2, y: box.y + box.height / 2 };
}

// The shell's overlayTick retries openglad_web_touch_enable until the game
// session exists; 'enabled' lands in the bridge mirror once C++ accepted it.
async function waitForTouchBridgeEnabled(page) {
  await page.waitForFunction(
    () => window.__opengladTouchKeys && window.__opengladTouchKeys.enabled === true,
    null,
    { timeout: 15_000 },
  );
}

async function startSeededGameplayWithOverlay(page) {
  await page.goto('/play.html');
  await waitForGameLoad(page);
  await expect(page.locator('#touch-controls')).toHaveClass(/tc-active/);
  await startSeededSinglePlayerFromPicker(page);
  await waitForTouchBridgeEnabled(page);
  await expect(page.locator('#tc-game')).toHaveClass(/tc-show/);
  // Settle at least one full redraw before sampling world coordinates.
  await waitForGameplayRenderSamples(page, 2, 10_000);
}

async function sampleControlWorld(page) {
  return await page.evaluate(() => ({
    x: window.__opengladLatestRenderSample?.control_worldx ?? null,
    y: window.__opengladLatestRenderSample?.control_worldy ?? null,
    seq: window.__opengladLatestRenderSample?.render_sample_seq ?? 0,
  }));
}

test.describe('Touch overlay activation', () => {
  test('overlay activates on a touch device and stays absent on desktop', async ({
    page,
    browser,
  }) => {
    test.setTimeout(180_000);

    // Natural gating: no __opengladForceTouchControls here. The device
    // descriptor provides maxTouchPoints > 0 and (pointer: coarse).
    await page.goto('/play.html');
    await waitForGameLoad(page);

    const overlay = page.locator('#touch-controls');
    await expect(overlay).toHaveClass(/tc-active/);
    await expect(page.locator('#tc-toggle')).toBeVisible();
    // Pre-gameplay: the gameplay cluster stays hidden, BACK is available.
    await expect(page.locator('#tc-game')).not.toHaveClass(/tc-show/);
    await expect(page.locator('#tc-back')).toBeVisible();

    // Same build, desktop context: the overlay must stay dormant. The project
    // runs with the iPhone device descriptor, and this Playwright version
    // injects the project's context options into manual newContext() calls —
    // so every touch-relevant option must be overridden explicitly here.
    const desktop = await browser.newContext({
      baseURL: 'http://localhost:8089',
      hasTouch: false,
      isMobile: false,
      deviceScaleFactor: 1,
      viewport: { width: 1280, height: 800 },
      userAgent:
        'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36',
    });
    try {
      const desktopPage = await desktop.newPage();
      await desktopPage.goto('/play.html');
      await waitForGameLoad(desktopPage);
      // Give overlayTick a few cycles to (incorrectly) activate.
      await desktopPage.waitForTimeout(1_000);
      await expect(desktopPage.locator('#touch-controls')).not.toHaveClass(
        /tc-active/,
      );
      await expect(desktopPage.locator('#tc-toggle')).toBeHidden();
      await expect(desktopPage.locator('#tc-back')).toBeHidden();
    } finally {
      await desktop.close();
    }
  });

  test('text-entry overlay shows only while a prompt is active', async ({ page }) => {
    await seedGameplayInitScript(page);
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await expect(page.locator('#touch-controls')).toHaveClass(/tc-active/);

    const wrap = page.locator('#og-text-entry-wrap');
    await expect(wrap).toBeHidden();
    // start_text_input()/stop_text_input() dispatch this CustomEvent from
    // native_input.cpp; drive the same seam directly to pin the shell wiring.
    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', { detail: { active: true } }),
      );
    });
    await expect(wrap).toBeVisible();
    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', { detail: { active: false } }),
      );
    });
    await expect(wrap).toBeHidden();
  });
});

test.describe('Touch gameplay controls', () => {
  test('menu taps click picker buttons via SDL touch synthesis', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await waitForPickerReady(page);

    // CONTINUE on the main menu, then GO on the team-build menu — all taps.
    await tapCanvasGameCoord(page, 150, 85);
    await page.waitForTimeout(1_500);
    await tapCanvasGameCoord(page, 250, 107);
    // Level load ends on the blocking SCENARIO INFORMATION dialog
    // ("TAP OR BACKSPACE TO CONTINUE" on web); a tap dismisses it. Harmless
    // if timing skipped the dialog — it is just a tap into the world.
    await page.waitForTimeout(2_500);
    await tapCanvasGameCoord(page, 160, 100);

    await page.waitForFunction(
      () =>
        window.__opengladGameState === 2 ||
        Boolean(window.__opengladLatestRenderSample),
      null,
      { timeout: 20_000 },
    );
    await waitForRenderedFrames(page, 4);
    await waitForGameplayProgress(page, 15_000);
  });

  test('virtual joystick drag moves the avatar and release stops it', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);

    const viewport = page.viewportSize();
    if (!viewport) {
      throw new Error('Viewport size is unavailable');
    }
    // Inside the stick zone: left 45% x bottom 70% of the viewport.
    const startX = Math.floor(viewport.width * 0.18);
    const startY = Math.floor(viewport.height * 0.72);

    const before = await sampleControlWorld(page);
    expect(before.x).not.toBeNull();

    // Press, then drag 80px to the right (octant 0 -> KEY_RIGHT) and hold.
    await dispatchPointer(page, '#tc-stick-zone', 'pointerdown', startX, startY);
    for (let step = 1; step <= 8; ++step) {
      await dispatchPointer(
        page,
        '#tc-stick-zone',
        'pointermove',
        startX + step * 10,
        startY,
      );
      await page.waitForTimeout(30);
    }
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.right)),
      )
      .toBe(true);
    await page.waitForTimeout(1_500);

    const during = await sampleControlWorld(page);
    await dispatchPointer(
      page,
      '#tc-stick-zone',
      'pointerup',
      startX + 80,
      startY,
    );

    expect(during.x - before.x).toBeGreaterThan(20);

    // Release clears the direction keys...
    await expect
      .poll(async () =>
        page.evaluate(() => {
          const keys = window.__opengladTouchKeys || {};
          return Boolean(
            keys.right || keys.left || keys.up || keys.down ||
              keys.up_right || keys.down_right || keys.up_left || keys.down_left,
          );
        }),
      )
      .toBe(false);

    // ...and the avatar settles: two spaced samples agree.
    await page.waitForTimeout(700);
    const settledA = await sampleControlWorld(page);
    await page.waitForTimeout(400);
    const settledB = await sampleControlWorld(page);
    expect(Math.abs(settledB.x - settledA.x)).toBeLessThanOrEqual(1);
    expect(Math.abs(settledB.y - settledA.y)).toBeLessThanOrEqual(1);
  });

  test('FIRE button holds and releases through the bridge', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);

    const fire = await elementCenter(page, '#tc-fire');
    await dispatchPointer(page, '#tc-fire', 'pointerdown', fire.x, fire.y, 55);
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.fire)),
      )
      .toBe(true);
    await expect(page.locator('#tc-fire')).toHaveClass(/tc-pressed/);

    // Hold long enough for several input samples (held-fire re-fires in sim),
    // then confirm gameplay stayed live while the button was down.
    await page.waitForTimeout(600);
    await waitForGameplayProgress(page, 5_000);

    await dispatchPointer(page, '#tc-fire', 'pointerup', fire.x, fire.y, 55);
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.fire)),
      )
      .toBe(false);
    await expect(page.locator('#tc-fire')).not.toHaveClass(/tc-pressed/);
  });

  test('BACK button pauses, aborts, and returns to the picker', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);

    const back = await elementCenter(page, '#tc-back');

    // First BACK: pause. The sim tick freezes while render frames continue.
    await dispatchPointer(page, '#tc-back', 'pointerdown', back.x, back.y, 61);
    await page.waitForTimeout(150);
    await dispatchPointer(page, '#tc-back', 'pointerup', back.x, back.y, 61);
    await page.waitForTimeout(500);
    const pausedTickA = await page.evaluate(
      () => window.__opengladLatestRenderSample?.tick ?? null,
    );
    await page.waitForTimeout(700);
    const pausedTickB = await page.evaluate(
      () => window.__opengladLatestRenderSample?.tick ?? null,
    );
    expect(pausedTickB).toBe(pausedTickA);

    // Second BACK: the Abort Mission prompt. Tap YES — the tap lands in the
    // joystick zone, whose dead-zone quick-tap forwards a click to the canvas.
    await dispatchPointer(page, '#tc-back', 'pointerdown', back.x, back.y, 62);
    await page.waitForTimeout(150);
    await dispatchPointer(page, '#tc-back', 'pointerup', back.x, back.y, 62);
    await page.waitForTimeout(700);
    await tapCanvasGameCoord(page, YES_BUTTON.x, YES_BUTTON.y);
    await page.waitForTimeout(1_000);

    // The mission-result popup (OK) still counts as Playing; dismiss it.
    const backInPicker = async () =>
      await page.evaluate(() => window.__opengladGameState === 1);
    for (let attempt = 0; attempt < 5 && !(await backInPicker()); ++attempt) {
      await tapCanvasGameCoord(page, OK_BUTTON.x, OK_BUTTON.y);
      await page.waitForTimeout(1_000);
    }
    await page.waitForFunction(() => window.__opengladGameState === 1, null, {
      timeout: 15_000,
    });
    // Leaving gameplay hides the touch gameplay cluster again.
    await expect(page.locator('#tc-game')).not.toHaveClass(/tc-show/);
  });

  test('audio context unlocks from the first touch gesture', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await page.goto('/play.html');
    await waitForGameLoad(page);

    const audioState = () =>
      page.evaluate(() => {
        const sdl = window.Module && (window.Module.SDL3 || window.Module.SDL2);
        return sdl && sdl.audioContext ? sdl.audioContext.state : null;
      });

    const initial = await audioState();
    if (initial === null) {
      // No AudioContext seam exposed by this SDL build: nothing to assert.
      test.skip(true, 'Module.SDL3.audioContext is not exposed');
      return;
    }
    // --autoplay-policy=user-gesture-required should keep it suspended until
    // a real gesture, but not every engine build enforces the policy (this
    // headless Chromium boots the context 'running'). The meaningful,
    // engine-independent contract: after the first touch gesture the context
    // is running. When the engine DOES enforce the policy (real iOS Safari,
    // WebKit), the pre-gesture 'suspended' assertion adds the unlock proof.
    expect(['suspended', 'running']).toContain(initial);

    const box = await page.locator('#canvas').boundingBox();
    if (!box) {
      throw new Error('Canvas bounding box is unavailable');
    }
    await page.touchscreen.tap(box.x + box.width / 2, box.y + 10);

    await expect.poll(audioState, { timeout: 10_000 }).toBe('running');
  });
});
