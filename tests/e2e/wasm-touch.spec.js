// @ts-check
// Touch-controls e2e suite. Runs under always-on Chromium projects for iPhone
// 15 landscape and portrait (hasTouch, isMobile, DPR 3, iOS UA) and, when
// OG_WEBKIT is set, under true WebKit in both phone orientations. Desktop
// chromium ignores this file (see playwright.config.js).
//
// The overlay lives in web/shell.html; held keys reach the engine through
// web_touch_bridge.cpp (Module._openglad_web_touch_set_key -> the
// touch_keystate seam OR-ed into isPlayerHoldingKey), and the bridge mirrors
// every accepted write to window.__opengladTouchKeys for these assertions.
const { test, expect } = require('@playwright/test');
const {
  getCanvasGameRegionScreenshot,
  getCanvasUiBox,
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

// Main-menu CONTINUE — center of the (80,75,68,20) CONTINUE half after the
// CONTINUE | LOAD split — and the team-build NETWORKING button (both on the
// 320x200 grid; same coords the wasm helpers navigate with; §2.10 ledger).
const CONTINUE_BUTTON = { x: 114, y: 85 };
// §2.5/§2.10: NETWORK is (182,178,56,18) on the base-camp strip.
const NETWORKING_BUTTON = { x: 210, y: 187 };
const NETWORKING_TITLE_REGION = { x: 60, y: 18, w: 200, h: 18 };
const TEAM_BUILD_NETWORKING_REGION = { x: 182, y: 178, w: 56, h: 18 };

// Web relay-first networking-menu ROOM CODE field (button id
// "network_room_value" in src/interface/ui/picker.cpp).
const ROOM_VALUE_REGION = { x: 110, y: 40, w: 160, h: 15 };
const ROOM_VALUE_CENTER = { x: 190, y: 47 };

// prompt_for_string draws its input line at (58, 60), 29 chars * 6px wide
// (src/interface/ui/level_editor_ui.cpp + text::input_string_ex).
const PROMPT_INPUT_REGION = { x: 56, y: 56, w: 184, h: 18 };

async function captureRegion(page, region) {
  return await getCanvasGameRegionScreenshot(
    page,
    region.x,
    region.y,
    region.w,
    region.h,
  );
}

async function waitForRegionToLeave(page, region, baseline, message) {
  await expect
    .poll(async () => (await captureRegion(page, region)).equals(baseline), {
      message,
      timeout: 15_000,
    })
    .toBe(false);
}

async function waitForRegionToMatch(page, region, baseline, message) {
  await expect
    .poll(async () => (await captureRegion(page, region)).equals(baseline), {
      message,
      timeout: 15_000,
    })
    .toBe(true);
}

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
  const ui = await getCanvasUiBox(page);
  return {
    x: ui.x + (gameX * ui.width) / 320,
    y: ui.y + (gameY * ui.height) / 200,
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

async function startSeededGameplayWithOverlay(page, pickerOptions = {}) {
  await page.goto('/play.html');
  await waitForGameLoad(page);
  await expect(page.locator('#touch-controls')).toHaveClass(/tc-active/);
  await startSeededSinglePlayerFromPicker(page, pickerOptions);
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
  test('one phone tap advances exactly one intro page until the picker', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    await page.goto('/play.html');
    await page.waitForFunction(
      () => window.__opengladIntroActive === true &&
        window.__opengladIntroTapReady === true,
      null,
      { timeout: 60_000 },
    );

    // Hold a touch across the first page's natural timeout. Its pointer-up
    // belongs to the old generation and must not skip the page that follows.
    const introBox = await page.locator('#canvas').boundingBox();
    if (!introBox) {
      throw new Error('Canvas bounding box is unavailable during intro');
    }
    const heldPoint = {
      x: introBox.x + introBox.width / 2,
      y: introBox.y + introBox.height / 2,
    };
    const heldGeneration = await page.evaluate(
      () => window.__opengladIntroTapGeneration,
    );
    await page.evaluate(({ x, y }) => {
      document.getElementById('canvas').dispatchEvent(
        new PointerEvent('pointerdown', {
          bubbles: true,
          cancelable: true,
          clientX: x,
          clientY: y,
          isPrimary: true,
          pointerId: 4242,
          pointerType: 'touch',
          button: 0,
          buttons: 1,
        }),
      );
    }, heldPoint);
    await page.waitForFunction(
      (generation) =>
        window.__opengladIntroTapGeneration > generation &&
        window.__opengladIntroTapReady === true,
      heldGeneration,
      { timeout: 30_000 },
    );
    await page.evaluate(({ x, y }) => {
      document.getElementById('canvas').dispatchEvent(
        new PointerEvent('pointerup', {
          bubbles: true,
          cancelable: true,
          clientX: x,
          clientY: y,
          isPrimary: true,
          pointerId: 4242,
          pointerType: 'touch',
          button: 0,
          buttons: 0,
        }),
      );
    }, heldPoint);
    await page.waitForTimeout(300);
    expect(
      await page.evaluate(() => window.__opengladIntroTapAdvanceCount || 0),
    ).toBe(0);
    expect(await page.evaluate(() => window.__opengladIntroActive)).toBe(true);

    const tapIntro = async (previousCount) => {
      const box = await page.locator('#canvas').boundingBox();
      if (!box) {
        throw new Error('Canvas bounding box is unavailable during intro');
      }
      await page.touchscreen.tap(
        box.x + box.width / 2,
        box.y + box.height / 2,
      );
      await page.waitForFunction(
        (count) =>
          (window.__opengladIntroTapAdvanceCount || 0) === count + 1,
        previousCount,
        { timeout: 10_000 },
      );
    };

    let advances = 0;
    await tapIntro(advances);
    advances += 1;
    // The completed tap's up event must not leak across the page boundary.
    await page.waitForTimeout(300);
    expect(
      await page.evaluate(() => window.__opengladIntroTapAdvanceCount || 0),
    ).toBe(advances);
    expect(await page.evaluate(() => window.__opengladIntroActive)).toBe(true);

    for (let pageIndex = 0; pageIndex < 10; ++pageIndex) {
      await page.waitForFunction(
        () => window.__opengladIntroActive === false ||
          window.__opengladIntroTapReady === true,
        null,
        { timeout: 20_000 },
      );
      if (!(await page.evaluate(() => window.__opengladIntroActive))) {
        break;
      }
      await tapIntro(advances);
      advances += 1;
    }
    await page.waitForFunction(
      () => window.__opengladIntroActive === false,
      null,
      { timeout: 20_000 },
    );
    expect(advances).toBeGreaterThan(1);
    await waitForGameLoad(page);
    await waitForPickerReady(page);
  });

  test('canvas fills the touch viewport and centers the fixed UI grid', async ({
    page,
  }, testInfo) => {
    await page.addInitScript(() => {
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);

    const geometry = await page.evaluate(() => {
      const canvas = document.getElementById('canvas');
      const rect = canvas.getBoundingClientRect();
      const viewport = window.visualViewport;
      return {
        canvas: {
          x: rect.x,
          y: rect.y,
          width: rect.width,
          height: rect.height,
        },
        viewport: {
          width: viewport && viewport.width > 0
            ? viewport.width
            : window.innerWidth,
          height: viewport && viewport.height > 0
            ? viewport.height
            : window.innerHeight,
        },
      };
    });
    expect(Math.abs(geometry.canvas.width - geometry.viewport.width)).toBeLessThanOrEqual(1);
    expect(Math.abs(geometry.canvas.height - geometry.viewport.height)).toBeLessThanOrEqual(1);

    const ui = await getCanvasUiBox(page);
    expect(ui.width / ui.height).toBeCloseTo(1.6, 2);
    expect(Math.abs(ui.x - (geometry.canvas.x + (geometry.canvas.width - ui.width) / 2)))
      .toBeLessThanOrEqual(1);
    expect(Math.abs(ui.y - (geometry.canvas.y + (geometry.canvas.height - ui.height) / 2)))
      .toBeLessThanOrEqual(1);

    if (testInfo.project.name.includes('portrait')) {
      expect(geometry.viewport.height).toBeGreaterThan(geometry.viewport.width);
    } else {
      expect(geometry.viewport.width).toBeGreaterThan(geometry.viewport.height);
    }
  });

  test('legacy WebKit fullscreen fallback handles its non-Promise vendor API', async ({
    page,
  }) => {
    await page.addInitScript(() => {
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Exercise the vendor branch deterministically on both the always-on
    // Chromium phone projects and the advisory true-WebKit projects. Legacy
    // Safari returns undefined from webkitRequestFullscreen, rather than a
    // Promise, and reports state through webkitfullscreenchange.
    await page.evaluate(() => {
      const target = document.documentElement;
      window.__opengladWebkitFullscreenCalls = 0;
      window.__opengladFakeWebkitFullscreenElement = null;

      Object.defineProperty(document, 'fullscreenEnabled', {
        configurable: true,
        value: false,
      });
      Object.defineProperty(document, 'webkitFullscreenEnabled', {
        configurable: true,
        value: true,
      });
      Object.defineProperty(document, 'webkitFullscreenElement', {
        configurable: true,
        get: () => window.__opengladFakeWebkitFullscreenElement,
      });
      Object.defineProperty(target, 'requestFullscreen', {
        configurable: true,
        value: undefined,
      });
      Object.defineProperty(target, 'webkitRequestFullscreen', {
        configurable: true,
        value: function webkitRequestFullscreenProbe() {
          window.__opengladWebkitFullscreenCalls += 1;
          window.__opengladFakeWebkitFullscreenElement = this;
          document.dispatchEvent(new Event('webkitfullscreenchange'));
          // Deliberately no return value: this is the legacy WebKit contract.
        },
      });
      document.dispatchEvent(new Event('webkitfullscreenchange'));
    });

    const fullscreenButton = page.getByRole('button', {
      name: 'Enter fullscreen',
    });
    await expect(fullscreenButton).toBeVisible();
    await fullscreenButton.click();
    await expect
      .poll(() =>
        page.evaluate(() => window.__opengladWebkitFullscreenCalls),
      )
      .toBe(1);
    await expect(page.getByRole('status')).toContainText('Fullscreen active');
    await expect(fullscreenButton).toBeHidden();
    await expect
      .poll(() => page.evaluate(() => document.activeElement?.id))
      .toBe('canvas');

    await page.evaluate(() => {
      window.__opengladFakeWebkitFullscreenElement = null;
      document.dispatchEvent(new Event('webkitfullscreenchange'));
    });
    await expect(fullscreenButton).toBeVisible();
    await expect(page.getByRole('status')).toHaveText('');
    await expect
      .poll(() => page.evaluate(() => document.activeElement?.id))
      .toBe('canvas');
  });

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
    const input = page.locator('#og-text-entry');
    await expect(wrap).toBeHidden();
    // Static noise suppression applies to every prompt. Room-code-specific
    // capitalization, length, and initial-value metadata arrive with the
    // native start_text_input event below.
    await expect(input).toHaveAttribute('autocorrect', 'off');
    await expect(input).toHaveAttribute('spellcheck', 'false');
    // start_text_input()/stop_text_input() dispatch this CustomEvent from
    // native_input.cpp; drive the same seam directly to pin the shell wiring.
    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', {
          detail: {
            active: true,
            initialValue: 'glad-e2e',
            maxBytes: 28,
            prompt: 'JOIN ROOM CODE',
            multiline: false,
          },
        }),
      );
    });
    await expect(wrap).toBeVisible();
    await expect(input).toHaveAttribute('autocapitalize', 'characters');
    await expect(input).toHaveAttribute('maxlength', '28');
    await expect(input).toHaveValue('GLAD-E2E');
    await expect(page.locator('#og-text-entry-cancel')).toBeVisible();

    // Multiline editor prompts keep their canvas-native controls; the
    // single-line DOM field must not cover or capture those buttons.
    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', {
          detail: { active: true, multiline: true },
        }),
      );
    });
    await expect(wrap).toBeHidden();

    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', { detail: { active: false } }),
      );
    });
    await expect(wrap).toBeHidden();
  });

  test('room-code prompt: DOM typing mirrors into the canvas, Enter accepts, CANCEL restores', async ({
    page,
  }) => {
    test.setTimeout(240_000);

    await page.addInitScript(() => {
      window.__opengladSkipIntroForTests = true;
      window.__opengladForceTouchControls = true;
      // The company-era main menu only has the CONTINUE half at (114,85)
      // when a company save exists — seed the standard web company so the
      // all-taps CONTINUE -> NETWORKING navigation below lands.
      window.__opengladSeedSinglePlayerTeam = true;
      window.__opengladRelayBaseUrlForTests = 'https://relay.test';
    });
    // Keep discovery deterministic and entirely local to Playwright. A DNS
    // failure can take as long as the prompt assertion timeout on some hosts.
    await page.route('https://relay.test/api/rooms**', (route) =>
      route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ rooms: [] }),
      }),
    );
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await waitForPickerReady(page);

    // All-taps navigation: CONTINUE -> team build -> NETWORKING.
    await tapCanvasGameCoord(page, CONTINUE_BUTTON.x, CONTINUE_BUTTON.y);
    await page.waitForTimeout(1_500);
    await tapCanvasGameCoord(page, NETWORKING_BUTTON.x, NETWORKING_BUTTON.y);
    await page.waitForTimeout(1_500);

    const wrap = page.locator('#og-text-entry-wrap');
    const input = page.locator('#og-text-entry');
    const fieldBaseline = await captureRegion(page, ROOM_VALUE_REGION);

    // Tapping the ROOM VALUE field opens the real C++ prompt; its
    // start_text_input() must surface the DOM overlay end to end.
    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    await expect(input).toHaveValue('');

    // iOS keyboard summoning path: while the prompt is up, a canvas tap must
    // focus the input from within that gesture (ownCanvasTouch redirect).
    await page.evaluate(() =>
      document.getElementById('og-text-entry').blur(),
    );
    await tapCanvasGameCoord(page, 160, 100);
    await expect
      .poll(async () =>
        page.evaluate(
          () => document.activeElement && document.activeElement.id,
        ),
      )
      .toBe('og-text-entry');

    // Type through the DOM input; every keystroke is mirrored into SDL, so
    // the in-canvas prompt must redraw with the typed code.
    const promptBaseline = await captureRegion(page, PROMPT_INPUT_REGION);
    await page.keyboard.type('GLAD-TEST', { delay: 60 });
    await expect(input).toHaveValue('GLAD-TEST');
    await waitForRegionToLeave(
      page,
      PROMPT_INPUT_REGION,
      promptBaseline,
      'typed characters should appear in the in-canvas prompt',
    );

    // Enter accepts: the prompt closes and the accepted code lands in the
    // networking menu's ROOM VALUE field.
    await page.keyboard.press('Enter');
    await expect(wrap).toBeHidden({ timeout: 10_000 });
    await waitForRegionToLeave(
      page,
      ROOM_VALUE_REGION,
      fieldBaseline,
      'the accepted room code should replace the ROOM VALUE field text',
    );
    const acceptedField = await captureRegion(page, ROOM_VALUE_REGION);

    // Reopen the prompt, type junk, then CANCEL: openglad_web_text_cancel
    // must deliver input_string's Escape path, restoring the original value.
    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    await expect(input).toHaveValue('GLAD-TEST');
    await tapCanvasGameCoord(page, 160, 100); // gesture-focus the input
    await expect
      .poll(async () =>
        page.evaluate(
          () => document.activeElement && document.activeElement.id,
        ),
      )
      .toBe('og-text-entry');
    await input.selectText();
    await page.keyboard.type('ZZZ', { delay: 60 });
    await expect(input).toHaveValue('ZZZ');
    const cancelButton = page.locator('#og-text-entry-cancel');
    await cancelButton.focus();
    await page.keyboard.press('Enter');
    await expect(wrap).toBeHidden({ timeout: 10_000 });
    await waitForRegionToMatch(
      page,
      ROOM_VALUE_REGION,
      acceptedField,
      'CANCEL should restore the previously accepted room code',
    );
  });
});

test.describe('Touch gameplay controls', () => {
  test('canvas touch bridge releases capture and clicks picker buttons', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await waitForPickerReady(page);

    // A browser gesture can revoke canvas pointer capture on iOS. The owned
    // mouse bridge must emit its matching up edge so menus do not stay stuck
    // in a held-click state.
    const blank = await canvasGameCoordToCss(page, 10, 190);
    await dispatchPointer(page, '#canvas', 'pointerdown', blank.x, blank.y, 39);
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchMouseDown)),
      )
      .toBe(true);
    await dispatchPointer(
      page,
      '#canvas',
      'lostpointercapture',
      blank.x,
      blank.y,
      39,
    );
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchMouseDown)),
      )
      .toBe(false);

    // CONTINUE on the main menu, then GO on the team-build menu — all taps.
    await tapCanvasGameCoord(page, CONTINUE_BUTTON.x, CONTINUE_BUTTON.y);
    await page.waitForTimeout(1_500);
    await tapCanvasGameCoord(page, 278, 187); // base-camp GO (244,178,68,18)
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

  test('virtual joystick holds diagonals through jitter and recovers capture loss', async ({ page }) => {
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

    // Establish UP_RIGHT just past the -22.5deg sector boundary.
    await dispatchPointer(page, '#tc-stick-zone', 'pointerdown', startX, startY);
    await dispatchPointer(
      page,
      '#tc-stick-zone',
      'pointermove',
      startX + 70,
      startY - 30,
    );
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.up_right)),
      )
      .toBe(true);

    // Real thumbs jitter by a pixel or two. Alternate across the raw octant
    // boundary for several sim samples; hysteresis must retain UP_RIGHT.
    // Target the canvas after the initial move to exercise the document-level
    // Safari fallback. Synthetic events do not establish real pointer capture.
    for (let step = 0; step < 16; ++step) {
      await dispatchPointer(
        page,
        '#canvas',
        'pointermove',
        startX + 70,
        startY + (step % 2 === 0 ? -30 : -28),
      );
      await page.waitForTimeout(20);
    }
    await expect
      .poll(async () => page.evaluate(() => ({
        upRight: Boolean(window.__opengladTouchKeys?.up_right),
        right: Boolean(window.__opengladTouchKeys?.right),
      })))
      .toEqual({ upRight: true, right: false });

    await page.waitForTimeout(1_200);
    const diagonal = await sampleControlWorld(page);
    expect(diagonal.x - before.x).toBeGreaterThan(10);
    expect(before.y - diagonal.y).toBeGreaterThan(10);

    // A deliberate turn well past the hysteresis margin changes exactly to
    // RIGHT; a lost capture then releases every held direction immediately.
    await dispatchPointer(
      page,
      '#canvas',
      'pointermove',
      startX + 80,
      startY,
    );
    await expect
      .poll(async () => page.evaluate(() => ({
        upRight: Boolean(window.__opengladTouchKeys?.up_right),
        right: Boolean(window.__opengladTouchKeys?.right),
      })))
      .toEqual({ upRight: false, right: true });
    await dispatchPointer(
      page,
      '#tc-stick-zone',
      'lostpointercapture',
      startX + 80,
      startY,
    );
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

    // Capture loss must not strand the joystick's pointer id: a fresh drag
    // starts normally, follows outside the zone, and releases on the document.
    await dispatchPointer(
      page,
      '#tc-stick-zone',
      'pointerdown',
      startX,
      startY,
      42,
    );
    await dispatchPointer(
      page,
      '#canvas',
      'pointermove',
      startX + 80,
      startY,
      42,
    );
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.right)),
      )
      .toBe(true);
    await dispatchPointer(
      page,
      '#canvas',
      'pointerup',
      startX + 80,
      startY,
      42,
    );

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

  test('FIRE button releases after capture loss or an off-button up', async ({ page }) => {
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

    await dispatchPointer(
      page,
      '#tc-fire',
      'lostpointercapture',
      fire.x,
      fire.y,
      55,
    );
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.fire)),
      )
      .toBe(false);
    await expect(page.locator('#tc-fire')).not.toHaveClass(/tc-pressed/);

    // A fresh press whose up lands on the canvas must also be released by the
    // document listener (the common Safari edge-gesture/capture-loss shape).
    await dispatchPointer(page, '#tc-fire', 'pointerdown', fire.x, fire.y, 56);
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.fire)),
      )
      .toBe(true);
    await dispatchPointer(page, '#canvas', 'pointerup', fire.x, fire.y, 56);
    await expect
      .poll(async () =>
        page.evaluate(() => Boolean(window.__opengladTouchKeys?.fire)),
      )
      .toBe(false);
  });

  test('BACK button pauses, aborts, and returns to the picker', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    let teamBuildNetworkingFace = null;
    await startSeededGameplayWithOverlay(page, {
      onTeamBuildReady: async () => {
        teamBuildNetworkingFace = await captureRegion(
          page,
          TEAM_BUILD_NETWORKING_REGION,
        );
      },
    });
    expect(teamBuildNetworkingFace).not.toBeNull();

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
    await waitForPickerReady(page, 1_000);
    // Leaving gameplay hides the touch gameplay cluster again.
    await expect(page.locator('#tc-game')).not.toHaveClass(/tc-show/);

    // Browser gameplay unwinds through the outer rAF state machine. Require
    // the exact NETWORKING button face captured from team build before the
    // game. A result-dialog tap at (160,140) overlaps SCENARIO on team build,
    // so this assertion also catches input leaking across the handoff.
    await waitForRegionToMatch(
      page,
      TEAM_BUILD_NETWORKING_REGION,
      teamBuildNetworkingFace,
      'post-game picker should remain on the exact team-build screen',
    );
    await page.waitForTimeout(1_000);
    expect(
      (await captureRegion(page, TEAM_BUILD_NETWORKING_REGION)).equals(
        teamBuildNetworkingFace,
      ),
      'post-game picker should not click through into Scenario after settling',
    ).toBe(true);
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
