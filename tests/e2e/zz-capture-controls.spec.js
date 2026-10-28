// @ts-check
// PR proof still for the controls follow-ups on the web build: the KEYMAP
// screen in a two-player mission, opened from player 2's PAUSED player
// screen, with player 2's LOOK UP on the Delete cap (#391) and the Mac
// note in the legend. A tool, not a test: skipped unless OG_WEB_SHOT_DIR
// names the output directory. The behaviour is checked elsewhere
// (wasm-game.spec.js "Modifier chords across seats", and KeymapScreen.* in
// og_test_abilities). The picture goes to the openglad-screenshots repo,
// never this one.
//
//   OG_WEB_SHOT_DIR=build/media/web npx playwright test \
//       tests/e2e/zz-capture-controls.spec.js --project=chromium
const path = require('node:path');
const { test } = require('@playwright/test');
const {
  clickCanvasGameCoord,
  focusCanvas,
  installChangelogSkip,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
  waitForRenderedFrames,
} = require('./wasm_helpers');

const SHOT_DIR = process.env.OG_WEB_SHOT_DIR;

function shotPath(name) {
  return path.join(/** @type {string} */ (SHOT_DIR), `${name}.png`);
}

test.describe('KEYMAP still (web)', () => {
  test.use({ viewport: { width: 1280, height: 800 } });

  test('keymap-web', async ({ page }) => {
    test.skip(!SHOT_DIR, 'set OG_WEB_SHOT_DIR to write the PR stills');
    test.setTimeout(180_000);
    await installChangelogSkip(page);
    await page.addInitScript(() => {
      window.__opengladSeedPlayerCount = 2;
      window.__opengladSkipIntroForTests = true;
    });
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await startSeededSinglePlayerFromPicker(page);
    await page.waitForFunction(() => window.__opengladNumViews === 2, null, {
      timeout: 15_000,
    });
    await focusCanvas(page);
    // Backspace is the web PAUSE key; the PAUSED menu lists P1 and P2
    // (src/interface/ui/pause_menu.cpp: P2's row is (90,130,140,15)).
    await page.keyboard.press('Backspace');
    await waitForRenderedFrames(page, 20);
    await clickCanvasGameCoord(page, 160, 137);
    await waitForRenderedFrames(page, 20);
    if (SHOT_DIR) await page.screenshot({ path: shotPath('keymap-web-p2-player-screen') });
    // KEYMAP sits at (84,169,66,18) on the player screen.
    await clickCanvasGameCoord(page, 117, 178);
    await waitForRenderedFrames(page, 30);
    await page.screenshot({ path: shotPath('keymap-web') });
  });
});
