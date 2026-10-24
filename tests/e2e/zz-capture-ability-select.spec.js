// @ts-check
// PR proof stills for New Ability Select on the web build: the touch overlay
// with its 1 / 2 / 3 SPEC buttons (seat 0's ability pictures, issue #384) at
// an iPhone and an iPad size, the iPhone again with ALT latched (the second
// row), and the
// CHANGELOG! page on a fresh desktop profile. A tool, not a test: every
// case is skipped unless OG_WEB_SHOT_DIR names the output directory, and the
// behaviour itself is checked by wasm-touch.spec.js and
// wasm-changelog.spec.js. The pictures go to the openglad-screenshots repo,
// never this one.
//
//   OG_WEB_SHOT_DIR=build/media/web npx playwright test \
//       tests/e2e/zz-capture-ability-select.spec.js --project=chromium
const path = require('node:path');
const { test, expect, devices } = require('@playwright/test');
const {
  installChangelogSkip,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
  waitForGameplayRenderSamples,
  waitForRenderedFrames,
} = require('./wasm_helpers');

const SHOT_DIR = process.env.OG_WEB_SHOT_DIR;

function shotPath(name) {
  return path.join(/** @type {string} */ (SHOT_DIR), `${name}.png`);
}

// A device descriptor without its browser choice (a describe group cannot
// switch browsers): these run on the chromium project, as the always-on
// touch projects do (playwright.config.js).
function onChromium(name) {
  const { defaultBrowserType, ...descriptor } = devices[name];
  return descriptor;
}

const TOUCH_DEVICES = [
  ['touch-iphone-landscape', 'iPhone 15 landscape', false],
  ['touch-ipad-landscape', 'iPad Pro 11 landscape', false],
  ['touch-iphone-alt', 'iPhone 15 landscape', true],
];

for (const [shot, device, altLatched] of TOUCH_DEVICES) {
  test.describe(`touch overlay still (${device})`, () => {
    // The chromium project's launch options (WebGL through SwiftShader)
    // stay; only the page's size, touch and pixel ratio change.
    test.use(onChromium(device));

    test(shot, async ({ page }) => {
      test.skip(!SHOT_DIR, 'set OG_WEB_SHOT_DIR to write the PR stills');
      test.setTimeout(180_000);
      await installChangelogSkip(page);
      await page.addInitScript(() => {
        window.__opengladSeedSinglePlayerTeam = true;
        window.__opengladSkipIntroForTests = true;
        window.__opengladForceTouchControls = true;
      });
      await page.goto('/play.html');
      await waitForGameLoad(page);
      await expect(page.locator('#touch-controls')).toHaveClass(/tc-active/);
      await startSeededSinglePlayerFromPicker(page);
      await page.waitForFunction(
        () => window.__opengladTouchKeys?.enabled === true,
        null,
        { timeout: 15_000 },
      );
      await expect(page.locator('#tc-game')).toHaveClass(/tc-show/);
      await page.waitForFunction(
        () => window.__opengladBelt?.beltHud === true,
        null,
        { timeout: 20_000 },
      );
      if (altLatched) {
        const alt = await page.locator('#tc-alt').boundingBox();
        await page.evaluate(({ x, y }) => {
          const el = document.getElementById('tc-alt');
          for (const type of ['pointerdown', 'pointerup']) {
            el.dispatchEvent(new PointerEvent(type, {
              pointerId: 71, pointerType: 'touch', isPrimary: true,
              clientX: x, clientY: y, bubbles: true, cancelable: true,
            }));
          }
        }, { x: alt.x + alt.width / 2, y: alt.y + alt.height / 2 });
        await page.waitForFunction(() => window.__opengladBelt?.shift === true,
          null, { timeout: 10_000 });
      }
      for (const selector of ['#tc-special', '#tc-spec2', '#tc-spec3']) {
        await expect(page.locator(selector)).toHaveClass(/tc-icon/);
      }
      await expect(page.locator('#tc-special .tc-key')).toHaveText(
        altLatched ? /^(S1|L\d+)$/ : '1');
      await expect(page.locator('#tc-spec3')).toBeVisible();
      await waitForGameplayRenderSamples(page, 4, 10_000);
      await page.screenshot({ path: shotPath(shot) });
    });
  });
}

test.describe('CHANGELOG! page still (web)', () => {
  test.use({ viewport: { width: 1280, height: 800 } });

  test('changelog-web', async ({ page }) => {
    test.skip(!SHOT_DIR, 'set OG_WEB_SHOT_DIR to write the PR stills');
    test.setTimeout(180_000);
    await page.goto('/play.html');
    await waitForGameLoad(page);
    await page.waitForFunction(
      () => window.__opengladChangelogState === 'open',
      null,
      { timeout: 90_000 },
    );
    // Past the page's fade-in.
    await waitForRenderedFrames(page, 60);
    await page.screenshot({ path: shotPath('changelog-web') });
  });
});
