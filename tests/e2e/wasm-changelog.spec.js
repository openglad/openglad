// @ts-check
// The CHANGELOG! page on the web build (src/interface/ui/changelog_screen.cpp,
// docs/ability-select-design.md F.2-F.3): a fresh browser profile boots
// through the intro to the main menu with no page; BEGIN NEW GAME brings the
// page, with the keyboard picture in its band; OK leads on into the company
// name screen; and after a reload BEGIN NEW GAME goes straight there,
// because the seen marker was flushed to IndexedDB, not merely saved in
// memory.
//
// This is the one spec that boots WITHOUT installChangelogSkip and without
// __opengladSkipIntroForTests (the engine skips the page for either flag).
const { test, expect } = require('@playwright/test');
const {
  clickCanvasGameCoord,
  getCanvasGameRegionScreenshot,
  waitForGameLoad,
  waitForPickerReady,
  waitForRenderedFrames,
} = require('./wasm_helpers');

// The page's picture band and its OK row on the 320x200 UI grid.
const BAND = { x: 4, y: 62, w: 312, h: 110 };
const OK_CENTRE = { x: 135 + 25, y: 178 + 8 };
// The main menu's BEGIN NEW GAME row (80,55,140,20), present with or
// without a company.
const BEGIN_CENTRE = { x: 80 + 70, y: 55 + 10 };

// The share of the band's pixels that are key-cap colours: the saturated
// player colours, each with red and green at least 40 apart (the page's
// dark-blue ink and its blends with the grey panel have them equal; the
// same rule as tests/integration/test_changelog_flow.cpp). Decoded in the page: the
// repo carries no PNG decoder for node.
async function keyCapShare(page) {
  const png = await getCanvasGameRegionScreenshot(
    page, BAND.x, BAND.y, BAND.w, BAND.h);
  return await page.evaluate(async (b64) => {
    const image = new Image();
    image.src = `data:image/png;base64,${b64}`;
    await image.decode();
    const canvas = document.createElement('canvas');
    canvas.width = image.width;
    canvas.height = image.height;
    const ctx = canvas.getContext('2d');
    ctx.drawImage(image, 0, 0);
    const { data } = ctx.getImageData(0, 0, canvas.width, canvas.height);
    let caps = 0;
    let total = 0;
    for (let i = 0; i < data.length; i += 16) {
      const r = data[i];
      const g = data[i + 1];
      const bl = data[i + 2];
      const hi = Math.max(r, g, bl);
      const lo = Math.min(r, g, bl);
      total += 1;
      if (hi >= 140 && hi - lo >= 90 && Math.abs(r - g) >= 40) caps += 1;
    }
    return total > 0 ? caps / total : 0;
  }, png.toString('base64'));
}

async function changelogState(page) {
  return await page.evaluate(() => window.__opengladChangelogState ?? null);
}

// Press BEGIN NEW GAME until the engine says what the page did. Emscripten
// forwards input only after a real click, so the first press may only wake
// it; a repeat lands on the page's text or the name screen, both inert there.
async function beginNewGame(page, expectedState) {
  for (let attempt = 0; attempt < 5; attempt += 1) {
    await clickCanvasGameCoord(page, BEGIN_CENTRE.x, BEGIN_CENTRE.y);
    try {
      await page.waitForFunction(
        (want) => window.__opengladChangelogState === want,
        expectedState,
        { timeout: 6_000 },
      );
      return;
    } catch {
      // not yet: press again
    }
  }
  expect(await changelogState(page)).toBe(expectedState);
}

test.describe('CHANGELOG! page (web)', () => {
  test('not at boot; BEGIN NEW GAME shows it once with the keyboard, OK leads on, a reload does not show it again', async ({
    page,
  }) => {
    test.setTimeout(240_000);
    const errors = [];
    page.on('pageerror', (err) => errors.push(`pageerror: ${err.message}`));

    await page.goto('/play.html');
    await waitForGameLoad(page);
    // The intro auto-advances into the main menu, and nothing else: the
    // page waits for the player to start a game.
    await waitForPickerReady(page);
    expect(await changelogState(page)).toBeNull();
    expect(await keyCapShare(page)).toBeLessThan(0.02);

    await beginNewGame(page, 'open');
    // The page fades in; wait until the picture is in its band.
    await expect
      .poll(() => keyCapShare(page), { timeout: 30_000, intervals: [500] })
      .toBeGreaterThan(0.2);

    await clickCanvasGameCoord(page, OK_CENTRE.x, OK_CENTRE.y);
    await page.waitForFunction(
      () => window.__opengladChangelogState === 'seen',
      null,
      { timeout: 30_000 },
    );
    // The company name screen replaces the page: the band no longer shows
    // the keyboard.
    await expect
      .poll(() => keyCapShare(page), { timeout: 30_000, intervals: [500] })
      .toBeLessThan(0.02);

    // Reload: the marker came back from IndexedDB, so the main menu boots
    // with no page and BEGIN NEW GAME finds nothing pending.
    await page.reload();
    await waitForGameLoad(page);
    await waitForPickerReady(page);
    expect(await changelogState(page)).toBeNull();
    await beginNewGame(page, 'none');
    await waitForRenderedFrames(page, 8);
    expect(await changelogState(page)).toBe('none');
    expect(await keyCapShare(page)).toBeLessThan(0.02);
    expect(errors).toEqual([]);
  });
});
