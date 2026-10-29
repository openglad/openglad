// @ts-check
// The click after a one-key rebind on the web build. A click on a line of
// the key list in the PAUSED player screen opens that key's prompt at the
// PRESS, so the mouse button comes up while the prompt waits for a key. The
// prompt used to throw that release away with every other event it does not
// answer, which left the game thinking the button was still held: the next
// click had no press to show and was ignored (seen on Firefox; the same in
// every browser and on the desktop). The real-menu twin is
// PauseMenuFlow.the_click_after_a_key_line_rebind_is_acted_on in
// tests/integration/test_pause_menu.cpp.
//
// Runs on chromium, and on Firefox when OG_FIREFOX_PATH names a Playwright
// Firefox build (see playwright.config.js).
const { test, expect } = require('@playwright/test');
const {
  clickCanvasGameCoord,
  focusCanvas,
  getCanvasGameRegionScreenshot,
  installChangelogSkip,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
  waitForRenderedFrames,
} = require('./wasm_helpers');

// The 320x200 UI grid (src/interface/ui/pause_menu.cpp, picker_sdl_defs.h):
// P1's row on PAUSED, and the first two ACTIONS lines of the key list
// (FIRE, SPEC 1: x 102..206, y 92..100 and 100..108).
const P1_ROW = { x: 160, y: 119 };
const FIRE_LINE = { x: 154, y: 96 };
const SPEC1_LINE = { x: 154, y: 104 };
// The band the key prompt's "REMAP CONTROLS" title covers (y 45) and the
// player screen's top buttons own otherwise.
const PROMPT_TITLE_BAND = { x: 100, y: 40, w: 120, h: 12 };

async function captureBand(page) {
  const r = PROMPT_TITLE_BAND;
  return await getCanvasGameRegionScreenshot(page, r.x, r.y, r.w, r.h);
}

async function captureSettledBand(page, description) {
  let settled = null;
  await expect
    .poll(
      async () => {
        const first = await captureBand(page);
        await waitForRenderedFrames(page, 6);
        const second = await captureBand(page);
        if (first.equals(second)) {
          settled = first;
          return true;
        }
        return false;
      },
      { message: `the band should settle: ${description}`, timeout: 20_000 },
    )
    .toBe(true);
  if (!settled) throw new Error(`never settled: ${description}`);
  return settled;
}

async function waitForPrompt(page, playerScreen, message, timeoutMs) {
  await expect
    .poll(async () => (await captureBand(page)).equals(playerScreen), {
      message,
      timeout: timeoutMs,
    })
    .toBe(false);
}

async function waitForPlayerScreen(page, playerScreen, message) {
  await expect
    .poll(async () => (await captureBand(page)).equals(playerScreen), {
      message,
      timeout: 20_000,
    })
    .toBe(true);
}

test.beforeEach(async ({ page }) => {
  await installChangelogSkip(page);
});

test.describe('Key list rebind (web)', () => {
  test('the click after a one-key rebind is acted on', async ({ page }) => {
    test.setTimeout(180_000);
    await page.addInitScript(() => {
      window.__opengladSeedSinglePlayerTeam = true;
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await startSeededSinglePlayerFromPicker(page);
    await focusCanvas(page);

    // Backspace is the web PAUSE key; then P1's player screen.
    await page.keyboard.press('Backspace');
    await waitForRenderedFrames(page, 20);
    await clickCanvasGameCoord(page, P1_ROW.x, P1_ROW.y);
    const playerScreen = await captureSettledBand(page, 'P1 player screen');

    // One click on the FIRE line: its prompt comes up, M answers it.
    await clickCanvasGameCoord(page, FIRE_LINE.x, FIRE_LINE.y);
    await waitForPrompt(page, playerScreen, 'the FIRE line opens its prompt', 20_000);
    await page.keyboard.press('KeyM', { delay: 75 });
    await waitForPlayerScreen(page, playerScreen, 'M closes the FIRE prompt');
    await waitForRenderedFrames(page, 10);

    // The NEXT click, once, on the SPEC 1 line: its prompt must come up.
    await clickCanvasGameCoord(page, SPEC1_LINE.x, SPEC1_LINE.y);
    await waitForPrompt(
      page,
      playerScreen,
      'the click after the FIRE rebind must open the SPEC 1 prompt',
      10_000,
    );
    await page.keyboard.press('KeyN', { delay: 75 });
    await waitForPlayerScreen(page, playerScreen, 'N closes the SPEC 1 prompt');
  });
});
