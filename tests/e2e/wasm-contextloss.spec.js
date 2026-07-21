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
  waitForGameLoad,
  waitForRenderedFrames,
} = require('./wasm_helpers');

// Same heuristic as wasm-game.spec.js: blank/near-blank canvas captures
// compress to well under this size, real frames do not.
const MIN_NON_TRIVIAL_PNG_BYTES = 2_000;

async function getCanvasScreenshot(page) {
  return await page.locator('#canvas').screenshot();
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
  }) => {
    // Boots the game twice (initial load + watchdog reload).
    test.setTimeout(150_000);
    // Shorten the 10s production watchdog so the fallback path stays inside
    // the test budget. The shell reads this before installing its listeners.
    await page.addInitScript(() => {
      /** @type {any} */ (window).__opengladContextRestoreWatchdogMs = 3_000;
    });

    await page.goto('/play.html');
    await waitForGameLoad(page);
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
    expect(hasVisualContent(await getCanvasScreenshot(page))).toBe(true);
  });
});

function hasVisualContent(buffer) {
  return buffer.length > MIN_NON_TRIVIAL_PNG_BYTES;
}
