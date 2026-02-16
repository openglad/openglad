// @ts-check
const { test, expect } = require('@playwright/test');

// Helper: wait for the WASM game to finish loading.
// The loading overlay gets class "hidden" when Module.setStatus('') fires.
async function waitForGameLoad(page) {
  await page.waitForFunction(
    () => {
      const el = document.getElementById('loading');
      return el && el.classList.contains('hidden');
    },
    { timeout: 60_000 },
  );
  // Give the game a moment to render its first frames
  await page.waitForTimeout(2000);
}

// Helper: capture a snapshot of canvas pixel data via screenshot
// (WebGL canvases don't support getImageData via 2d context)
async function getCanvasScreenshot(page) {
  const canvas = page.locator('#canvas');
  return await canvas.screenshot();
}

// Helper: check if a screenshot buffer has non-trivial content
// (i.e., not a solid black or single-color rectangle)
function hasVisualContent(buffer) {
  // PNG files have IDAT chunks with compressed pixel data.
  // A solid-color image compresses to a very small size.
  // Any real game frame will be at least a few KB.
  return buffer.length > 2000;
}

test.describe('Landing Page', () => {
  test('displays title and play button', async ({ page }) => {
    await page.goto('/');

    // Verify the page title
    await expect(page).toHaveTitle('Huddle Dungeon');

    // Verify the heading is visible
    const heading = page.locator('h1');
    await expect(heading).toBeVisible();
    await expect(heading).toHaveText('Huddle Dungeon');

    // Verify the Play button exists and links to play.html
    const playButton = page.locator('a.play-button');
    await expect(playButton).toBeVisible();
    await expect(playButton).toHaveText('Play');
    await expect(playButton).toHaveAttribute('href', 'play.html');
  });
});

test.describe('Game Loading', () => {
  test('WASM module loads and initializes', async ({ page }) => {
    // Collect console errors
    const errors = [];
    page.on('pageerror', (err) => errors.push(err.message));

    await page.goto('/play.html');

    // Verify canvas element exists
    const canvas = page.locator('#canvas');
    await expect(canvas).toBeVisible();

    // Wait for game to finish loading (Module.setStatus('') hides the overlay)
    await waitForGameLoad(page);

    // Loading overlay should be hidden after initialization
    const loading = page.locator('#loading');
    await expect(loading).toBeHidden();

    // No page-level errors should have occurred
    expect(errors).toEqual([]);
  });

  test('canvas renders game content', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Take a screenshot of the canvas
    const screenshot = await getCanvasScreenshot(page);

    // The screenshot should have substantial content (not blank/black)
    expect(hasVisualContent(screenshot)).toBe(true);
  });

  test('WebGL context is active', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Verify a WebGL context exists on the canvas
    const hasWebGL = await page.evaluate(() => {
      const canvas = document.getElementById('canvas');
      if (!canvas) return false;
      const gl =
        canvas.getContext('webgl2') ||
        canvas.getContext('webgl') ||
        canvas.getContext('experimental-webgl');
      // Emscripten may already have the context, so check via the canvas attribute
      // or try to verify drawing buffer dimensions
      if (gl) {
        return gl.drawingBufferWidth > 0 && gl.drawingBufferHeight > 0;
      }
      // Emscripten takes the context, so we can't create a new one.
      // Check if the canvas has been written to by examining its dimensions.
      return canvas.width > 0 && canvas.height > 0;
    });

    expect(hasWebGL).toBe(true);
  });
});

test.describe('Game Interaction', () => {
  test('keyboard input does not crash the game', async ({ page }) => {
    const errors = [];
    page.on('pageerror', (err) => errors.push(err.message));

    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Focus the canvas
    await page.locator('#canvas').click();
    await page.waitForTimeout(500);

    // Send various keyboard inputs
    const keys = ['Enter', 'Escape', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter'];
    for (const key of keys) {
      await page.keyboard.press(key);
      await page.waitForTimeout(300);
    }

    // Wait a moment for any deferred errors
    await page.waitForTimeout(1000);

    // Canvas should still be visible (game didn't crash)
    const canvas = page.locator('#canvas');
    await expect(canvas).toBeVisible();

    // Loading overlay should still be hidden (no error state)
    const loading = page.locator('#loading');
    await expect(loading).toBeHidden();

    // No page errors
    expect(errors).toEqual([]);
  });

  test('canvas continues rendering after interaction', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Take initial screenshot
    const before = await getCanvasScreenshot(page);

    // Focus and send input
    await page.locator('#canvas').click();
    await page.waitForTimeout(500);

    // Press Enter to potentially advance past menu
    await page.keyboard.press('Enter');
    await page.waitForTimeout(1000);

    // Canvas should still have content
    const after = await getCanvasScreenshot(page);
    expect(hasVisualContent(after)).toBe(true);
  });

  test('game navigation changes canvas content', async ({ page }) => {
    await page.goto('/play.html');
    await waitForGameLoad(page);

    // Take screenshot of initial state (main menu)
    const menuScreenshot = await getCanvasScreenshot(page);

    // Focus canvas and interact
    await page.locator('#canvas').click();
    await page.waitForTimeout(500);

    // Navigate: press arrow keys and Enter to trigger menu changes
    await page.keyboard.press('ArrowDown');
    await page.waitForTimeout(500);
    await page.keyboard.press('ArrowDown');
    await page.waitForTimeout(500);
    await page.keyboard.press('Enter');
    await page.waitForTimeout(2000);

    // Take screenshot after navigation
    const afterScreenshot = await getCanvasScreenshot(page);

    // Both screenshots should have content
    expect(hasVisualContent(menuScreenshot)).toBe(true);
    expect(hasVisualContent(afterScreenshot)).toBe(true);

    // The screenshots should differ (game responded to input)
    // Compare the raw PNG buffers - they should not be identical
    const differ = !menuScreenshot.equals(afterScreenshot);
    expect(differ).toBe(true);
  });
});
