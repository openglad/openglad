// @ts-check
// The CHANGELOG! page on the web build (src/interface/ui/changelog_screen.cpp,
// docs/ability-select-design.md F.2-F.3): a fresh browser profile boots
// through the intro into the page, with the keyboard picture in its band;
// OK leads to the main menu; and after a reload the page is gone, because
// the seen marker was flushed to IndexedDB, not merely saved in memory.
//
// This is the one spec that boots WITHOUT installChangelogSkip and without
// __opengladSkipIntroForTests (the engine skips the page for either flag).
const { test, expect } = require('@playwright/test');
const {
  clickCanvasGameCoord,
  getCanvasGameRegionScreenshot,
  waitForGameLoad,
  waitForRenderedFrames,
} = require('./wasm_helpers');

// The page's picture band and its OK row on the 320x200 UI grid.
const BAND = { x: 4, y: 62, w: 312, h: 110 };
const OK_CENTRE = { x: 135 + 25, y: 178 + 8 };

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

test.describe('CHANGELOG! page (web)', () => {
  test('shows once with the keyboard, OK reaches the main menu, a reload does not show it again', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    page.on('pageerror', (err) => errors.push(`pageerror: ${err.message}`));

    await page.goto('/play.html');
    await waitForGameLoad(page);
    // The intro auto-advances into the picker, which opens the page first.
    await page.waitForFunction(
      () => window.__opengladChangelogState === 'open',
      null,
      { timeout: 90_000 },
    );
    // The page fades in; wait until the picture is in its band.
    await expect
      .poll(() => keyCapShare(page), { timeout: 30_000, intervals: [500] })
      .toBeGreaterThan(0.2);

    // Emscripten forwards input only after a real click; OK is the first.
    await clickCanvasGameCoord(page, OK_CENTRE.x, OK_CENTRE.y);
    await page.waitForFunction(
      () => window.__opengladChangelogState === 'seen',
      null,
      { timeout: 30_000 },
    );
    // The main menu replaces the page: the band no longer shows the keyboard.
    await expect
      .poll(() => keyCapShare(page), { timeout: 30_000, intervals: [500] })
      .toBeLessThan(0.02);

    // Reload: the marker came back from IndexedDB, so nothing is pending.
    await page.reload();
    await waitForGameLoad(page);
    await page.waitForFunction(
      () => window.__opengladChangelogState === 'none',
      null,
      { timeout: 90_000 },
    );
    await waitForRenderedFrames(page, 8);
    expect(await changelogState(page)).toBe('none');
    expect(await keyCapShare(page)).toBeLessThan(0.02);
    expect(errors).toEqual([]);
  });
});
