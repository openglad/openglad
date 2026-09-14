// @ts-check
//
// WebGL context loss/restore survival (§9.6, playtest item F3).
//
// The iPad task-switch failure mode: the browser discards the WebGL context
// while the tab is backgrounded, 'webglcontextlost' fires on return, and the
// old shell answered with a modal alert and a dead canvas. The shell now
// preventDefault()s the loss (enabling restoration), shows "RESTORING
// GRAPHICS…" status text, and on 'webglcontextrestored' notifies the wasm
// side, which rebuilds its renderer and textures at the next present.
// Content self-heals because every present re-uploads the CPU-side canvases.
// If the browser never restores the context, a watchdog reloads the page:
// IDBFS autosave makes the reload lossless.
//
// WEBGL_lose_context drives both paths deterministically.
const { test, expect } = require('@playwright/test');
const {
  getCanvasGameRegionScreenshot,
  waitForGameLoad,
  waitForPickerReady,
  waitForRenderedFrames,
} = require('./wasm_helpers');

// Same heuristic as wasm-game.spec.js: blank/near-blank canvas captures
// compress to well under this size, real frames do not.
const MIN_NON_TRIVIAL_PNG_BYTES = 2_000;

// The CONTINUE half of the main menu's second row on the 320x200 UI reference
// grid (src/interface/ui/menu_screen_specs.cpp: "continue_game" is 68x20 at
// 80,79; the mutually exclusive "no_company_note" is the 140-wide
// "NO COMPANY YET" row at the same y). This one band therefore tells the
// with-company main menu apart from the fresh-install one.
const MAIN_MENU_CONTINUE_REGION = { x: 80, y: 75, w: 68, h: 20 };

async function getCanvasScreenshot(page) {
  return await page.locator('#canvas').screenshot();
}

async function captureRegion(page, region) {
  return await getCanvasGameRegionScreenshot(
    page,
    region.x,
    region.y,
    region.w,
    region.h,
  );
}

// Capture a region only once two consecutive captures a few frames apart are
// byte-identical: the picker redraws every frame from deterministic state, so
// a settled band is a reliable reference across a page reload.
async function captureSettledRegion(page, region, description, timeoutMs = 30_000) {
  let settled = null;
  await expect
    .poll(
      async () => {
        const first = await captureRegion(page, region);
        await page.waitForTimeout(250);
        const second = await captureRegion(page, region);
        if (first.equals(second)) {
          settled = first;
          return true;
        }
        return false;
      },
      { message: `region should settle: ${description}`, timeout: timeoutMs },
    )
    .toBe(true);
  if (!settled) {
    throw new Error(`region never settled: ${description}`);
  }
  return settled;
}

// Grab the live drawing context Emscripten created and stash its
// WEBGL_lose_context extension for the test body.
async function armLoseContextExtension(page) {
  return await page.evaluate(() => {
    const module = /** @type {any} */ (window).Module;
    const canvas = document.getElementById('canvas');
    const gl =
      (module && (module.ctx || module.GLctx)) ||
      (canvas &&
        (canvas.getContext('webgl2') ||
          canvas.getContext('webgl') ||
          canvas.getContext('experimental-webgl')));
    if (!gl) {
      return false;
    }
    const ext = gl.getExtension('WEBGL_lose_context');
    if (!ext) {
      return false;
    }
    /** @type {any} */ (window).__pwLoseContext = ext;
    return true;
  });
}

test.describe('WebGL context loss recovery', () => {
  test('lose/restore recreates the renderer in place with no alert', async ({
    page,
  }) => {
    // Two full settles plus the restore round-trip can brush the default
    // 60s budget under swiftshader.
    test.setTimeout(120_000);
    const dialogs = [];
    page.on('dialog', async (dialog) => {
      dialogs.push(dialog.message());
      await dialog.dismiss();
    });

    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Archive-DCE guard: the KEEPALIVE export must have survived into the
    // shipped runtime (and be visible on Module).
    const playJs = await page.request.get('/play.js');
    expect(await playJs.text()).toContain(
      'openglad_web_notify_context_restored',
    );
    expect(
      await page.evaluate(
        () =>
          typeof (/** @type {any} */ (window).Module)
            ._openglad_web_notify_context_restored === 'function',
      ),
    ).toBe(true);

    expect(await armLoseContextExtension(page)).toBe(true);
    expect(hasVisualContent(await getCanvasScreenshot(page))).toBe(true);

    // Lose the context: the shell must show the restoring status instead of
    // an alert, and must arm (but not yet fire) the reload watchdog.
    await page.evaluate(() =>
      /** @type {any} */ (window).__pwLoseContext.loseContext(),
    );
    await expect
      .poll(() =>
        page.evaluate(
          () =>
            /** @type {any} */ (window).__opengladContextLossState.lostEvents,
        ),
      )
      .toBeGreaterThan(0);
    await expect(page.locator('#loading')).toBeVisible();
    await expect(page.locator('#loading-text')).toContainText(
      'RESTORING GRAPHICS',
    );

    // Let a few presents run on the lost context: GL calls must be silently
    // ignored, not fatal, while the loss window is open.
    await page.waitForTimeout(750);

    await page.evaluate(() =>
      /** @type {any} */ (window).__pwLoseContext.restoreContext(),
    );
    await expect
      .poll(() =>
        page.evaluate(() => {
          const state = /** @type {any} */ (window).__opengladContextLossState;
          return state.restoredEvents > 0 && state.notifyCalls > 0;
        }),
      )
      .toBe(true);
    await expect(page.locator('#loading')).toBeHidden();

    // The wasm side rebuilds the renderer at the next present; the canvas
    // must be repainting real content again shortly afterwards.
    await waitForRenderedFrames(page, 10, 10_000);
    await expect
      .poll(async () => (await getCanvasScreenshot(page)).length, {
        timeout: 15_000,
      })
      .toBeGreaterThan(MIN_NON_TRIVIAL_PNG_BYTES);

    const finalState = await page.evaluate(
      () => /** @type {any} */ (window).__opengladContextLossState,
    );
    expect(finalState.reloadFallbacks).toBe(0);
    expect(dialogs).toEqual([]);
  });

  test('watchdog reloads into the autosaved state when restore never comes', async ({
    page,
    browser,
  }) => {
    // Boots the game three times (control context + initial load + reload).
    test.setTimeout(240_000);
    // Shorten the 10s production watchdog so the fallback path stays inside
    // the test budget. The shell reads this before installing its listeners.
    //
    // The company is seeded on the FIRST boot only. window.__opengladSeed*
    // flags are re-read at every boot (web_runtime_diagnostics.cpp), so
    // seeding unconditionally would re-found the company after the reload and
    // hide a build whose saves never reach IndexedDB. sessionStorage survives
    // window.location.reload() in the same tab, so the reloaded page can only
    // get its company back out of IDBFS.
    await page.addInitScript(() => {
      /** @type {any} */ (window).__opengladContextRestoreWatchdogMs = 3_000;
      /** @type {any} */ (window).__opengladSkipIntroForTests = true;
      if (!window.sessionStorage.getItem('og_seeded_company')) {
        window.sessionStorage.setItem('og_seeded_company', '1');
        /** @type {any} */ (window).__opengladSeedSinglePlayerTeam = true;
      }
    });

    // SaveData::save() -> sync_filesystem() logs this from FS.syncfs(false)'s
    // success callback (src/resources/platform_io.cpp). It is a SEQUENCING
    // gate, not a tooth: syncfs reports success for an empty or stubbed IDBFS
    // mount too, so the line only says the autosave's sync has completed and
    // the context may be killed. What proves the save actually reached
    // IndexedDB is the post-reload pixel comparison at the end: the reloaded
    // picker must come back on the with-company main menu.
    const idbfsSyncs = [];
    page.on('console', (msg) => {
      if (msg.text().includes('IDBFS saved to IndexedDB')) {
        idbfsSyncs.push(msg.text());
      }
    });

    // CONTROL: the same build in a storage-clean context boots the
    // fresh-install main menu. Capturing it proves this band actually
    // distinguishes the two main-menu variants, so the post-reload
    // comparison below is not comparing a screen to itself.
    // The project's own baseURL, never a second copy of it: playwright.config.js
    // is the one place the dev server's port is written down.
    const cleanContext = await browser.newContext({
      baseURL: test.info().project.use.baseURL,
    });
    let freshMenu;
    try {
      const cleanPage = await cleanContext.newPage();
      await cleanPage.addInitScript(() => {
        /** @type {any} */ (window).__opengladSkipIntroForTests = true;
      });
      await cleanPage.goto('/play.html');
      await waitForGameLoad(cleanPage);
      await waitForPickerReady(cleanPage);
      freshMenu = await captureSettledRegion(
        cleanPage,
        MAIN_MENU_CONTINUE_REGION,
        'fresh-install main menu',
      );
    } finally {
      await cleanContext.close();
    }

    await page.goto('/play.html');
    await waitForGameLoad(page);
    await waitForPickerReady(page);

    const withCompanyMenu = await captureSettledRegion(
      page,
      MAIN_MENU_CONTINUE_REGION,
      'with-company main menu',
    );
    expect(
      withCompanyMenu.equals(freshMenu),
      'the seeded company must change the main menu (CONTINUE vs NO COMPANY YET)',
    ).toBe(false);
    await expect
      .poll(() => idbfsSyncs.length > 0, {
        message:
          "the company autosave's IDBFS sync must complete before the context "
          + 'is killed (sequencing gate, not the persistence proof)',
        timeout: 20_000,
      })
      .toBe(true);

    expect(await armLoseContextExtension(page)).toBe(true);

    const reloaded = page.waitForEvent('load', { timeout: 30_000 });
    await page.evaluate(() =>
      /** @type {any} */ (window).__pwLoseContext.loseContext(),
    );
    await expect(page.locator('#loading-text')).toContainText(
      'RESTORING GRAPHICS',
    );

    // No restoreContext(): the watchdog must reload the page, and the game
    // must boot back up from persisted state.
    await reloaded;
    await waitForGameLoad(page);
    await waitForPickerReady(page);
    expect(hasVisualContent(await getCanvasScreenshot(page))).toBe(true);

    // The "into the autosaved state" half of the promise: the reloaded picker
    // must come back on the with-company main menu, not the fresh-install one.
    const reloadedMenu = await captureSettledRegion(
      page,
      MAIN_MENU_CONTINUE_REGION,
      'post-reload main menu',
    );
    expect(
      reloadedMenu.equals(freshMenu),
      'the watchdog reload must not land on the NO COMPANY YET main menu',
    ).toBe(false);
    expect(
      reloadedMenu.equals(withCompanyMenu),
      'the watchdog reload must restore the same company main menu from IDBFS',
    ).toBe(true);
  });
});

function hasVisualContent(buffer) {
  return buffer.length > MIN_NON_TRIVIAL_PNG_BYTES;
}
