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
  installChangelogSkip,
  readButtonPixels,
} = require('./wasm_helpers');

// Every boot in this file skips the once-only CHANGELOG! page
// (tests/e2e/wasm-changelog.spec.js is the one that shows it).
test.beforeEach(async ({ page }) => {
  await installChangelogSkip(page);
});

// Abort-flow dialog buttons on the 320x200 UI reference grid
// (src/interface/ui/picker_dialogs.cpp): YES center and the popup OK center.
const YES_BUTTON = { x: 95, y: 140 };
const OK_BUTTON = { x: 160, y: 140 };
// PAUSED menu QUIT MISSION row center (src/interface/ui/pause_menu.cpp:
// column x=90 w=140, QUIT at y=68 h=15).
const QUIT_MISSION_BUTTON = { x: 160, y: 75 };

// Main-menu CONTINUE — center of the (80,75,68,20) CONTINUE half after the
// CONTINUE | LOAD split — and the team-build NETWORKING button (both on the
// 320x200 grid; same coords the wasm helpers navigate with; §2.10 ledger).
const CONTINUE_BUTTON = { x: 114, y: 85 };
// §2.5/§2.10: NETWORK is (200,178,56,18) on the base-camp strip, which
// re-gridded when DIFFICULTY joined it (docs/camp-controls-design.md).
const NETWORKING_BUTTON = { x: 228, y: 187 };
const NETWORKING_TITLE_REGION = { x: 60, y: 18, w: 200, h: 18 };
const TEAM_BUILD_NETWORKING_REGION = { x: 200, y: 178, w: 56, h: 18 };

// Web relay-first networking-menu ROOM CODE field (button id
// "network_room_value" in src/interface/ui/picker.cpp).
const ROOM_VALUE_REGION = { x: 110, y: 40, w: 160, h: 15 };
const ROOM_VALUE_CENTER = { x: 190, y: 47 };

// prompt_for_string draws its input line at (58, 60), 29 chars * 6px wide,
// with the shared ACCEPT/CANCEL footer at y=70..84 (text::input_string_ex).
// Keep the capture frame tall enough to include that footer as well as text.
const PROMPT_INPUT_REGION = { x: 56, y: 56, w: 184, h: 36 };
const PROMPT_ACCEPT_BUTTON = { x: 191, y: 79 };

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

    // #249: the phone classification (touch + short screen edge) must reach
    // the engine — openglad_web_set_single_seat_device mirrors every
    // accepted write to window.__opengladSingleSeat, like the touch keys.
    await expect
      .poll(() => page.evaluate(() => window.__opengladSingleSeat), {
        timeout: 15_000,
      })
      .toBe(true);

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
      await installChangelogSkip(desktopPage);
      await desktopPage.goto('/play.html');
      await waitForGameLoad(desktopPage);
      // Give overlayTick a few cycles to (incorrectly) activate.
      await desktopPage.waitForTimeout(1_000);
      await expect(desktopPage.locator('#touch-controls')).not.toHaveClass(
        /tc-active/,
      );
      await expect(desktopPage.locator('#tc-toggle')).toBeHidden();
      await expect(desktopPage.locator('#tc-back')).toBeHidden();
      // A desktop must be explicitly classified multi-seat (false, not
      // merely unset — proving the export was called with 0).
      await expect
        .poll(() => desktopPage.evaluate(() => window.__opengladSingleSeat), {
          timeout: 15_000,
        })
        .toBe(false);
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

    // Re-arm a SINGLE-LINE prompt first. Without it the deactivate leg below
    // starts from an already-hidden wrapper (the multiline prompt hid it), so
    // it can never observe a visible->hidden transition and a shell that
    // ignored active:false would stay green.
    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', {
          detail: {
            active: true,
            initialValue: 'x',
            maxBytes: 28,
            prompt: 'JOIN ROOM CODE',
            multiline: false,
          },
        }),
      );
    });
    await expect(wrap).toBeVisible();
    expect(
      await page.evaluate(() => window.__opengladTextInputActive),
    ).toBe(true);

    // stop_text_input() (src/interface/input/native_input.cpp) dispatches
    // active:false: the shell must hide the wrapper, clear the seam flag and
    // blur the field regardless of the previous prompt's multiline flag.
    await page.evaluate(() => {
      window.dispatchEvent(
        new CustomEvent('openglad-text-input', { detail: { active: false } }),
      );
    });
    await expect(wrap).toBeHidden();
    expect(
      await page.evaluate(() => window.__opengladTextInputActive),
    ).toBe(false);
    await expect
      .poll(() => page.evaluate(() => document.activeElement?.id ?? ''))
      .not.toBe('og-text-entry');
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

    // Return accepts: the prompt closes and the accepted code lands in the
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

    // The shared prompt also exposes an on-canvas ACCEPT control. Tap its
    // logical center rather than relying on the native keyboard, which is the
    // path available to a phone user who has dismissed the keyboard.
    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    await expect(input).toHaveValue('GLAD-TEST');
    await tapCanvasGameCoord(page, 160, 100); // gesture-focus the input
    await input.selectText();
    await page.keyboard.type('TOUCH-OK', { delay: 60 });
    await expect(input).toHaveValue('TOUCH-OK');
    await tapCanvasGameCoord(
      page,
      PROMPT_ACCEPT_BUTTON.x,
      PROMPT_ACCEPT_BUTTON.y,
    );
    await expect(wrap).toBeHidden({ timeout: 10_000 });
    await waitForRegionToLeave(
      page,
      ROOM_VALUE_REGION,
      acceptedField,
      'on-canvas ACCEPT should commit the edited room code',
    );
    const touchAcceptedField = await captureRegion(page, ROOM_VALUE_REGION);
    expect(touchAcceptedField.equals(acceptedField)).toBe(false);

    // Reopen the prompt, type junk, then CANCEL: openglad_web_text_cancel
    // must deliver input_string's Escape path, restoring the original value.
    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    await expect(input).toHaveValue('TOUCH-OK');
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
      touchAcceptedField,
      'CANCEL should restore the previously accepted room code',
    );
  });

  test('room-code prompt: physical typing before the field is touched leaves no residue', async ({
    page,
  }) => {
    test.setTimeout(240_000);

    await page.addInitScript(() => {
      window.__opengladSkipIntroForTests = true;
      window.__opengladForceTouchControls = true;
      window.__opengladSeedSinglePlayerTeam = true;
      window.__opengladRelayBaseUrlForTests = 'https://relay.test';
    });
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

    await tapCanvasGameCoord(page, CONTINUE_BUTTON.x, CONTINUE_BUTTON.y);
    await page.waitForTimeout(1_500);
    await tapCanvasGameCoord(page, NETWORKING_BUTTON.x, NETWORKING_BUTTON.y);
    await page.waitForTimeout(1_500);

    const wrap = page.locator('#og-text-entry-wrap');
    const input = page.locator('#og-text-entry');

    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    await expect(input).toHaveValue('');

    // A convertible or a paired keyboard can type straight into the engine
    // while the DOM field is still untouched. Those keystrokes edit the C++
    // buffer with no way to tell the field about them.
    await page.evaluate(() => document.getElementById('og-text-entry').blur());
    const promptBaseline = await captureRegion(page, PROMPT_INPUT_REGION);
    await page.keyboard.type('ABC', { delay: 60 });
    await waitForRegionToLeave(
      page,
      PROMPT_INPUT_REGION,
      promptBaseline,
      'physically typed characters should reach the in-canvas prompt',
    );
    await expect(input).toHaveValue('');

    // The field is now authoritative: its first edit must replace the whole
    // buffer, so what the player sees is what the prompt accepts.
    await tapCanvasGameCoord(page, 160, 100); // gesture-focus the input
    await expect
      .poll(async () =>
        page.evaluate(() => document.activeElement && document.activeElement.id),
      )
      .toBe('og-text-entry');
    await page.keyboard.type('X', { delay: 60 });
    await expect(input).toHaveValue('X');
    const submitted = await input.inputValue();

    await page.keyboard.press('Enter');
    await expect(wrap).toBeHidden({ timeout: 10_000 });

    // Reopening the prompt reads the accepted C++ buffer back out through
    // start_text_input's initialValue.
    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    expect(
      await page.evaluate(
        () =>
          window.__opengladTextInputDetail &&
          window.__opengladTextInputDetail.initialValue,
      ),
    ).toBe(submitted);

    // Second leg: the field can also LOSE focus mid-prompt. A first edit
    // makes it authoritative, but a paired keyboard keeps typing into the
    // engine the moment focus leaves, so the next edit after a refocus is
    // in exactly the position the first one was: the field's value is the
    // only trustworthy state, and the buffer must be replaced whole again.
    await tapCanvasGameCoord(page, 160, 100); // gesture-focus the input
    await expect
      .poll(async () =>
        page.evaluate(() => document.activeElement && document.activeElement.id),
      )
      .toBe('og-text-entry');
    await input.selectText();
    await page.keyboard.press('Backspace');
    await page.keyboard.type('X', { delay: 60 });
    await expect(input).toHaveValue('X');

    const blurBaseline = await captureRegion(page, PROMPT_INPUT_REGION);
    await page.evaluate(() => document.getElementById('og-text-entry').blur());
    await page.keyboard.type('YZ', { delay: 60 });
    await waitForRegionToLeave(
      page,
      PROMPT_INPUT_REGION,
      blurBaseline,
      'typing after the field blurs should still reach the in-canvas prompt',
    );
    await expect(input).toHaveValue('X');

    await tapCanvasGameCoord(page, 160, 100); // gesture-refocus the input
    await expect
      .poll(async () =>
        page.evaluate(() => document.activeElement && document.activeElement.id),
      )
      .toBe('og-text-entry');
    await page.keyboard.press('End');
    await page.keyboard.type('W', { delay: 60 });
    await expect(input).toHaveValue('XW');
    const submittedAfterBlur = await input.inputValue();

    await page.keyboard.press('Enter');
    await expect(wrap).toBeHidden({ timeout: 10_000 });

    await tapCanvasGameCoord(page, ROOM_VALUE_CENTER.x, ROOM_VALUE_CENTER.y);
    await expect(wrap).toBeVisible({ timeout: 10_000 });
    const readbackAfterBlur = await page.evaluate(
      () =>
        window.__opengladTextInputDetail &&
        window.__opengladTextInputDetail.initialValue,
    );
    expect(readbackAfterBlur).not.toContain('YZ');
    expect(readbackAfterBlur).toBe(submittedAfterBlur);
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
    await tapCanvasGameCoord(page, 287, 187); // base-camp GO (262,178,50,18)
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

  // New Ability Select (on by default): the SPEC cluster reads 1 2 3, and
  // SPEC 2 / SPEC 3 reach the engine's touch seam as key slots 17 and 18
  // (the C++ mirror names them spec2 / spec3 after its bounds check).
  for (const [selector, name] of [['#tc-spec2', 'spec2'], ['#tc-spec3', 'spec3']]) {
    test(`${name} button delivers its belt key slot`, async ({ page }) => {
      test.setTimeout(180_000);

      await seedGameplayInitScript(page);
      await startSeededGameplayWithOverlay(page);

      await expect
        .poll(async () => page.evaluate(() => window.__opengladAbilitySelect))
        .toBe(true);
      // The badges read the seeded soldier's slots: CHARGE ready, the
      // other two locked until level 4 and 7 (issue #384).
      await expect(page.locator('#tc-special')).toHaveText('1');
      await expect(page.locator('#tc-spec2')).toHaveText('L4');
      await expect(page.locator('#tc-spec3')).toHaveText('L7');
      await expect(page.locator('#tc-spec3')).toBeVisible();

      const center = await elementCenter(page, selector);
      await dispatchPointer(page, selector, 'pointerdown', center.x, center.y, 57);
      await expect
        .poll(async () =>
          page.evaluate((key) => Boolean(window.__opengladTouchKeys?.[key]), name),
        )
        .toBe(true);
      await page.waitForTimeout(200);
      await dispatchPointer(page, selector, 'pointerup', center.x, center.y, 57);
      await expect
        .poll(async () =>
          page.evaluate((key) => Boolean(window.__opengladTouchKeys?.[key]), name),
        )
        .toBe(false);
    });
  }

  // The 1 / 2 / 3 buttons never cover the radar. The radar's block is the
  // HUD's bottom-right 64x48 (radar_block_for_pane: at most 60x44, 4 in
  // from the pane's edges) on the gameplay HUD canvas the engine publishes
  // (__opengladCanvasDiagnostics.gameplay_ui_*), which is stretched over the
  // whole #canvas. Checked at this project's phone size and at the iPad
  // Pro 11 sizes, landscape and portrait (the PR stills' tablet). The FIRE,
  // BACK and LOOK-row placements are older and tracked as issue #382.
  test('1 / 2 / 3 buttons stay clear of the radar on phone and tablet', async ({ page }) => {
    test.setTimeout(240_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);
    await expect(page.locator('#tc-spec3')).toBeVisible();

    const phone = page.viewportSize();
    if (!phone) {
      throw new Error('viewport size is unavailable');
    }
    const sizes = [
      ['phone', phone],
      ['iPad Pro 11 landscape', { width: 1194, height: 834 }],
      ['iPad Pro 11 portrait', { width: 834, height: 1194 }],
    ];
    for (const [label, size] of sizes) {
      await page.setViewportSize(size);
      // The engine re-lays its HUD canvas to the new aspect: wait until the
      // published HUD canvas has the canvas box's shape (16:10 or wider:
      // 200 tall; narrower: 320 wide).
      await expect
        .poll(
          async () =>
            page.evaluate(() => {
              const d = window.__opengladCanvasDiagnostics;
              const c = document.getElementById('canvas');
              if (!d || !c) {
                return false;
              }
              const r = c.getBoundingClientRect();
              const tall = r.width * 200 < r.height * 320;
              const want = tall
                ? Math.round((r.height * 320) / r.width)
                : Math.round((r.width * 200) / r.height);
              const got = tall ? d.gameplay_ui_height : d.gameplay_ui_width;
              return (
                Math.abs(r.width - window.innerWidth) <= 1 &&
                Math.abs(r.height - window.innerHeight) <= 1 &&
                (tall ? d.gameplay_ui_width === 320 : d.gameplay_ui_height === 200) &&
                Math.abs(got - want) <= 1
              );
            }),
          { message: `${label}: HUD canvas follows the viewport`, timeout: 20_000 },
        )
        .toBe(true);

      const geometry = await page.evaluate(() => {
        const d = window.__opengladCanvasDiagnostics;
        const c = document.getElementById('canvas').getBoundingClientRect();
        const sx = c.width / d.gameplay_ui_width;
        const sy = c.height / d.gameplay_ui_height;
        const radar = {
          left: c.left + (d.gameplay_ui_width - 64) * sx,
          top: c.top + (d.gameplay_ui_height - 48) * sy,
          right: c.left + (d.gameplay_ui_width - 4) * sx,
          bottom: c.top + (d.gameplay_ui_height - 4) * sy,
        };
        const rect = (id) => {
          const r = document.getElementById(id).getBoundingClientRect();
          return { left: r.left, top: r.top, right: r.right, bottom: r.bottom };
        };
        return {
          radar,
          buttons: {
            '1': rect('tc-special'),
            '2': rect('tc-spec2'),
            '3': rect('tc-spec3'),
          },
          viewport: { width: window.innerWidth, height: window.innerHeight },
        };
      });
      const r = geometry.radar;
      for (const [name, b] of Object.entries(geometry.buttons)) {
        const overlaps =
          b.left < r.right && b.right > r.left && b.top < r.bottom && b.bottom > r.top;
        expect(
          overlaps,
          `${label}: button ${name} ${JSON.stringify(b)} covers the radar ${JSON.stringify(r)}`,
        ).toBe(false);
        expect(b.left, `${label}: button ${name} is on screen`).toBeGreaterThanOrEqual(0);
        expect(b.right).toBeLessThanOrEqual(geometry.viewport.width);
        expect(b.top).toBeGreaterThanOrEqual(0);
        expect(b.bottom).toBeLessThanOrEqual(geometry.viewport.height);
      }
    }
  });

  // Issue #384: the 1 / 2 / 3 buttons are seat 0's hotbar slots. The engine
  // pushes each slot's state and picture ('openglad-belt', mirrored on
  // window.__opengladBelt; web_touch_bridge.cpp); the page paints the bytes
  // it was sent into the button's 16x16 canvas. The seeded soldier shows
  // CHARGE ready, BOOMERANG locked until level 4 and WHIRLWIND until 7.
  const SPEC_SELECTORS = ['#tc-special', '#tc-spec2', '#tc-spec3'];

  async function waitForBeltOnButtons(page) {
    await expect
      .poll(
        async () =>
          page.evaluate(() => {
            const b = window.__opengladBelt;
            return Boolean(b && b.beltHud && b.cells && b.cells.length === 3);
          }),
        { message: 'the engine pushed seat 0 slots', timeout: 20_000 },
      )
      .toBe(true);
  }

  async function beltMirror(page) {
    return await page.evaluate(() => {
      const b = window.__opengladBelt;
      return {
        beltHud: b.beltHud,
        hudHidden: b.hudHidden,
        shift: b.shift,
        cells: b.cells.map((c) => ({
          key: c.key,
          label: c.label,
          state: c.state,
          pressed: c.pressed,
          drawn: c.drawn,
          locked: c.locked,
          pictureKey: c.pictureKey,
        })),
      };
    });
  }

  // Every button shows exactly the bytes the engine sent for its slot, and
  // they are a real picture (opaque pixels, transparent corners); an empty
  // slot is its key alone.
  async function expectButtonsShowTheirSlots(page) {
    const mirror = await beltMirror(page);
    for (let i = 0; i < 3; i++) {
      const selector = SPEC_SELECTORS[i];
      const cell = mirror.cells[i];
      await expect(page.locator(selector)).toHaveClass(/tc-icon/);
      await expect(page.locator(`${selector} .tc-key`)).toHaveText(cell.label);
      if (cell.state === 'empty') {
        expect(cell.pictureKey).toBe('');
        await expect(page.locator(selector)).toHaveClass(/tc-empty/);
        await expect(page.locator(`${selector} canvas.tc-icon-px`)).toBeHidden();
        continue;
      }
      await expect
        .poll(async () => (await readButtonPixels(page, selector)).key, {
          message: `${selector} painted its slot's picture`,
        })
        .toBe(cell.pictureKey);
      const px = await readButtonPixels(page, selector);
      expect(px.sent, `${selector}: the engine sent ${cell.pictureKey}`).not.toBeNull();
      expect(px.painted).toEqual(px.sent);
      let opaque = 0;
      for (let p = 3; p < px.painted.length; p += 4) {
        opaque += px.painted[p] === 255 ? 1 : 0;
      }
      expect(opaque, `${selector}: a picture, not an empty square`).toBeGreaterThan(30);
      expect(px.painted[3], `${selector}: the corner stays clear`).toBe(0);
    }
    return mirror;
  }

  test('spec buttons show the seat\'s belt pictures', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);
    await waitForBeltOnButtons(page);

    const mirror = await expectButtonsShowTheirSlots(page);
    expect(mirror.shift).toBe(false);
    expect(mirror.cells.map((c) => c.label)).toEqual(['1', 'L4', 'L7']);
    expect(mirror.cells.map((c) => c.state)).toEqual(['castable', 'locked', 'locked']);
    expect(mirror.cells[0].drawn).toBe('icons/charge.png');
    expect(mirror.cells[1].drawn).toBe('icons/boomerang.png');
    expect(mirror.cells[2].drawn).toBe('icons/whirlwind.png');
    expect(mirror.cells.map((c) => c.locked)).toEqual([false, true, true]);
    // The button text is the badge alone (the canvas holds no text).
    await expect(page.locator('#tc-special')).toHaveText('1');

    // A locked slot is the engine's grey copy (no CSS filter): every opaque
    // pixel is a grey, and the button is dimmed.
    const locked = await readButtonPixels(page, '#tc-spec2');
    for (let p = 0; p < locked.painted.length; p += 4) {
      if (locked.painted[p + 3] === 0) {
        continue;
      }
      expect(locked.painted[p], `pixel ${p / 4} is grey`).toBe(locked.painted[p + 1]);
      expect(locked.painted[p + 1]).toBe(locked.painted[p + 2]);
    }
    const ready = await readButtonPixels(page, '#tc-special');
    let coloured = 0;
    for (let p = 0; p < ready.painted.length; p += 4) {
      coloured += ready.painted[p + 3] === 255 &&
        (ready.painted[p] !== ready.painted[p + 1] ||
          ready.painted[p + 1] !== ready.painted[p + 2]) ? 1 : 0;
    }
    expect(coloured, 'CHARGE keeps its colours').toBeGreaterThan(10);
    await expect(page.locator('#tc-spec2')).toHaveClass(/tc-locked/);
    await expect(page.locator('#tc-spec3')).toHaveClass(/tc-locked/);
    await expect(page.locator('#tc-special')).not.toHaveClass(/tc-locked/);
    const opacity = await page.evaluate(() =>
      ['tc-special', 'tc-spec2'].map((id) =>
        getComputedStyle(document.getElementById(id)).opacity),
    );
    expect(opacity).toEqual(['0.6', '0.42']);
    // Pixel-sharp at 3x.
    const rendering = await page.evaluate(() =>
      getComputedStyle(document.querySelector('#tc-special canvas')).imageRendering);
    expect(rendering).toBe('pixelated');
  });

  test('ALT swaps the buttons to the SHIFTER row and back', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);
    await waitForBeltOnButtons(page);
    const plain = await expectButtonsShowTheirSlots(page);
    expect(plain.shift).toBe(false);
    const plainKeys = plain.cells.map((c) => c.pictureKey);

    const alt = await elementCenter(page, '#tc-alt');
    await dispatchPointer(page, '#tc-alt', 'pointerdown', alt.x, alt.y, 61);
    await dispatchPointer(page, '#tc-alt', 'pointerup', alt.x, alt.y, 61);
    await expect(page.locator('#tc-alt')).toHaveClass(/tc-latched/);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladBelt.shift), {
        message: 'the engine resolved the SHIFTER row',
      })
      .toBe(true);
    const shifted = await expectButtonsShowTheirSlots(page);
    expect(shifted.cells[0].pictureKey).not.toBe(plainKeys[0]);
    for (const cell of shifted.cells) {
      expect(cell.label, 'the second row is lettered S<n> or L<n>').toMatch(/^(S[123]|L\d+)$/);
    }

    await dispatchPointer(page, '#tc-alt', 'pointerdown', alt.x, alt.y, 62);
    await dispatchPointer(page, '#tc-alt', 'pointerup', alt.x, alt.y, 62);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladBelt.shift))
      .toBe(false);
    const back = await expectButtonsShowTheirSlots(page);
    expect(back.cells.map((c) => c.pictureKey)).toEqual(plainKeys);
  });

  test('spec buttons show the held key white, a short slot red, a locked one dim', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);
    await waitForBeltOnButtons(page);
    await expectButtonsShowTheirSlots(page);

    const borderOf = (id) =>
      page.evaluate((i) => getComputedStyle(document.getElementById(i)).borderTopColor, id);

    // Held: the page's own tc-pressed (bindHeldButton), a white edge; the
    // engine echoes the held key in its state a frame later.
    const spec1 = await elementCenter(page, '#tc-special');
    await dispatchPointer(page, '#tc-special', 'pointerdown', spec1.x, spec1.y, 63);
    await expect(page.locator('#tc-special')).toHaveClass(/tc-pressed/);
    expect(await borderOf('tc-special')).toBe('rgb(255, 255, 255)');
    await expect
      .poll(async () => page.evaluate(() => window.__opengladBelt.cells[0].pressed))
      .toBe(true);
    // A locked slot never lights.
    const spec2 = await elementCenter(page, '#tc-spec2');
    await dispatchPointer(page, '#tc-spec2', 'pointerdown', spec2.x, spec2.y, 64);
    await expect(page.locator('#tc-spec2')).toHaveClass(/tc-pressed/);
    expect(await borderOf('tc-spec2')).not.toBe('rgb(255, 255, 255)');
    expect(
      await page.evaluate(() => getComputedStyle(document.getElementById('tc-spec2')).opacity),
    ).toBe('0.42');
    await page.waitForTimeout(150);
    await dispatchPointer(page, '#tc-spec2', 'pointerup', spec2.x, spec2.y, 64);
    await dispatchPointer(page, '#tc-special', 'pointerup', spec1.x, spec1.y, 63);
    await expect(page.locator('#tc-special')).not.toHaveClass(/tc-pressed/);

    // Short: the engine's 'short' (resolve_belt_cells, the HUD's own rule,
    // pinned by the native HUD tests) turns the button's edge red. The
    // seeded soldier's MP comes back faster than a test can spend it, so
    // the page is handed the engine's last push with slot 1 short, then the
    // real push again.
    // Read in the same task as the dispatch, so a real push (the engine
    // echoing the key release) cannot land in between.
    const look = await page.evaluate(() => {
      const real = window.__opengladBelt;
      const short = { ...real, cells: real.cells.map((c, i) =>
        (i === 0 ? { ...c, state: 'short', pressed: false } : c)) };
      window.dispatchEvent(new CustomEvent('openglad-belt', { detail: short }));
      const one = document.getElementById('tc-special');
      const two = document.getElementById('tc-spec2');
      const result = {
        short: one.classList.contains('tc-short'),
        border: getComputedStyle(one).borderTopColor,
        otherBorder: getComputedStyle(two).borderTopColor,
      };
      window.dispatchEvent(new CustomEvent('openglad-belt', { detail: real }));
      return result;
    });
    expect(look.short).toBe(true);
    expect(look.border).toBe('rgb(255, 64, 64)');
    expect(look.otherBorder).not.toBe('rgb(255, 64, 64)');
    await expect(page.locator('#tc-special')).not.toHaveClass(/tc-short/);
  });

  // While the overlay is up seat 0's pane draws no hotbar: hudHidden is the
  // engine's own decision (new_score_panel), copied to the page. Turning the
  // overlay off (the gamepad toggle) brings the strip back to the pane's
  // bottom-left; turning it on again takes it away.
  test('the seat-0 hotbar is not drawn while the overlay is on', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await startSeededGameplayWithOverlay(page);
    await waitForBeltOnButtons(page);
    expect((await beltMirror(page)).hudHidden).toBe(true);

    // The pane's bottom-left, where the strip sits (HUD canvas units,
    // stretched over the whole #canvas): 64 wide, the bottom 48 rows.
    const hudRegion = async () => {
      const clip = await page.evaluate(() => {
        const d = window.__opengladCanvasDiagnostics;
        const r = document.getElementById('canvas').getBoundingClientRect();
        const sx = r.width / d.gameplay_ui_width;
        const sy = r.height / d.gameplay_ui_height;
        return { x: r.left + 2 * sx, y: r.top + r.height - 48 * sy, width: 62 * sx, height: 20 * sy };
      });
      return await page.screenshot({ clip });
    };
    // The fraction of pixels two captures disagree on. Weather (rain) and
    // the palette's colour cycling touch a few scattered pixels; the strip
    // covers most of the clip.
    const differs = async (one, two) => page.evaluate(async ([x, y]) => {
      const load = async (b64) => {
        const img = new Image();
        img.src = `data:image/png;base64,${b64}`;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = img.width;
        c.height = img.height;
        const ctx = c.getContext('2d');
        ctx.drawImage(img, 0, 0);
        return ctx.getImageData(0, 0, c.width, c.height).data;
      };
      const a = await load(x);
      const b = await load(y);
      let off = 0;
      for (let i = 0; i < a.length; i += 4) {
        off += Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) +
          Math.abs(a[i + 2] - b[i + 2]) > 48 ? 1 : 0;
      }
      return off / (a.length / 4);
    }, [one.toString('base64'), two.toString('base64')]);
    const hidden = await hudRegion();

    const toggle = page.locator('#tc-toggle');
    await toggle.click();
    await expect(page.locator('#touch-controls')).not.toHaveClass(/tc-active/);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladBelt.hudHidden), {
        message: 'overlay off: the engine draws the hotbar again',
      })
      .toBe(false);
    await expect
      .poll(async () => differs(await hudRegion(), hidden), {
        message: 'the strip is back in the pane',
        timeout: 15_000,
      })
      .toBeGreaterThan(0.3);

    await toggle.click();
    await expect(page.locator('#touch-controls')).toHaveClass(/tc-active/);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladBelt.hudHidden), {
        message: 'overlay on again: the pane hides the hotbar',
      })
      .toBe(true);
    await expect
      .poll(async () => differs(await hudRegion(), hidden), {
        message: 'the pane looks as it did with the overlay up',
        timeout: 15_000,
      })
      .toBeLessThan(0.1);
  });

  // With the setting off (Game Settings > UI FX > New Ability Select) the
  // cluster is today's: SPC casts, SP+ changes the special, and there is no
  // third button. Flipped back on, the 1 2 3 labels return. All taps.
  test('turning New Ability Select off relabels SPC / SP+ and hides SPEC 3', async ({ page }) => {
    test.setTimeout(180_000);

    await seedGameplayInitScript(page);
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await waitForPickerReady(page);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladAbilitySelect))
      .toBe(true);
    await expect(page.locator('#tc-special')).toHaveText('1');
    await expect(page.locator('#tc-spec2')).toHaveText('2');
    await expect(page.locator('#tc-spec3')).not.toHaveClass(/hidden/);

    // Main menu GAME SETTINGS (80,128,140,15), then UI FX (130,102,90,15),
    // then the New Ability Select row (100,104,120,15).
    await tapCanvasGameCoord(page, 150, 135);
    await page.waitForTimeout(1_500);
    await tapCanvasGameCoord(page, 175, 109);
    await page.waitForTimeout(1_500);
    await tapCanvasGameCoord(page, 160, 111);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladAbilitySelect), {
        timeout: 15_000,
      })
      .toBe(false);
    await expect(page.locator('#tc-special')).toHaveText('SPC');
    await expect(page.locator('#tc-spec2')).toHaveText('SP+');
    await expect(page.locator('#tc-spec3')).toHaveClass(/hidden/);
    // The relabel goes through the badge: the picture canvases survive it,
    // hidden while the buttons are plain.
    for (const selector of ['#tc-special', '#tc-spec2', '#tc-spec3']) {
      await expect(page.locator(selector)).not.toHaveClass(/tc-icon/);
      await expect(page.locator(`${selector} canvas.tc-icon-px`)).toHaveCount(1);
      await expect(page.locator(`${selector} .tc-key`)).toHaveCount(1);
    }
    await expect(page.locator('#tc-special canvas.tc-icon-px')).toBeHidden();

    await tapCanvasGameCoord(page, 160, 111);
    await expect
      .poll(async () => page.evaluate(() => window.__opengladAbilitySelect), {
        timeout: 15_000,
      })
      .toBe(true);
    await expect(page.locator('#tc-special')).toHaveText('1');
    await expect(page.locator('#tc-spec2')).toHaveText('2');
    await expect(page.locator('#tc-spec3')).not.toHaveClass(/hidden/);
    for (const selector of ['#tc-special', '#tc-spec2', '#tc-spec3']) {
      await expect(page.locator(`${selector} canvas.tc-icon-px`)).toHaveCount(1);
    }
  });

  test('BACK button opens the pause menu, QUIT aborts, and returns to the picker', async ({ page }) => {
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

    // BACK opens the PAUSED menu; the world pauses underneath it. The sim
    // tick freezes while the (asyncify-suspended) render samples stop
    // advancing.
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

    // QUIT MISSION on the pause menu, then YES on the Abort Mission confirm.
    // The menu column is x=90 w=140 (row centers x=160): RESUME y=32,
    // RESTART y=50, QUIT y=68, h=15 — QUIT center is (160, 75) on the
    // 320x200 UI grid (src/interface/ui/pause_menu.cpp). The YES tap lands
    // in the joystick zone, whose dead-zone quick-tap forwards a click.
    await tapCanvasGameCoord(page, QUIT_MISSION_BUTTON.x, QUIT_MISSION_BUTTON.y);
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
    // game. A result-dialog tap at (160,140) lands in the zone band on team
    // build — on a Multiplayer Arenas company that band is the docket's
    // SETUP row, the SETUP wizard's one door on this client (PR #307 round
    // 2, R2-5/R2-9) — so this assertion also catches input leaking across
    // the handoff. The docket row itself is the SAME 10 px face the SDL
    // flows click, and the web client IS the SDL client: its door evidence
    // is tests/integration/test_campaign_zone_ui.cpp's
    // versus_docket_page_rows_open_the_wizard_not_the_submenu and the
    // keyboard-spine leg beside it, not a second tap chain here.
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
    // --autoplay-policy=user-gesture-required should keep it suspended until a
    // real gesture, but not every engine build enforces the policy (this
    // headless Chromium boots the context 'running').
    expect(['suspended', 'running']).toContain(initial);

    // A 'running' context makes the gesture irrelevant: unlockAudioContext()
    // (web/shell.html:796-808) only resumes when state === 'suspended', so
    // deleting the shell's pointerdown/touchend listeners entirely would leave
    // a 'toBe("running")' poll green. Suspend first so the gesture is
    // load-bearing, and spy on resume() so the resume is ATTRIBUTED to the
    // gesture: SDL's own silence-timer resume happens outside any DOM event,
    // and that is exactly the out-of-gesture resume iOS Safari rejects.
    //
    // The attribution comes from a capture-phase marker of our own rather than
    // from the deprecated `window.event` (Chromium/WebKit only; undefined in
    // Firefox, where every resume would have been reported as 'none'). Capture
    // runs window -> document, and the shell installs its unlock listeners on
    // `document` with { capture: true }, so the marker is already set when
    // unlockAudioContext() calls resume(); a macrotask clears it once the whole
    // dispatch is over, so the silence-timer resume still reports 'none'.
    await page.evaluate(async () => {
      const sdl = window.Module && (window.Module.SDL3 || window.Module.SDL2);
      const ctx = sdl.audioContext;
      window.__pwResumeCalls = [];
      window.__pwGestureType = null;
      const markGesture = (event) => {
        window.__pwGestureType = event.type;
        setTimeout(() => {
          window.__pwGestureType = null;
        }, 0);
      };
      window.addEventListener('pointerdown', markGesture, true);
      window.addEventListener('touchend', markGesture, true);
      const realResume = ctx.resume.bind(ctx);
      ctx.resume = function () {
        window.__pwResumeCalls.push(window.__pwGestureType || 'none');
        return realResume();
      };
      if (ctx.state === 'running') {
        await ctx.suspend();
      }
    });
    await expect.poll(audioState, { timeout: 10_000 }).toBe('suspended');

    const box = await page.locator('#canvas').boundingBox();
    if (!box) {
      throw new Error('Canvas bounding box is unavailable');
    }
    await page.touchscreen.tap(box.x + box.width / 2, box.y + 10);

    await expect
      .poll(
        () =>
          page.evaluate(() =>
            (window.__pwResumeCalls || []).some(
              (type) => type === 'pointerdown' || type === 'touchend',
            ),
          ),
        {
          message:
            'the first touch gesture must resume the AudioContext from inside '
            + 'its own pointerdown/touchend handler',
          timeout: 10_000,
        },
      )
      .toBe(true);
    await expect.poll(audioState, { timeout: 10_000 }).toBe('running');
  });
});
