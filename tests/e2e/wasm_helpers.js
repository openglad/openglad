// @ts-check

async function ensureRenderTicker(page) {
  await page.evaluate(() => {
    if (window.__pwRenderTickerReady) {
      return;
    }
    window.__pwRenderTickerReady = true;
    window.__pwRenderFrameCount = 0;
    const tick = () => {
      window.__pwRenderFrameCount += 1;
      window.requestAnimationFrame(tick);
    };
    window.requestAnimationFrame(tick);
  });
}

async function waitForRenderedFrames(page, frameDelta = 1, timeoutMs = 5_000) {
  await ensureRenderTicker(page);
  const initialCount = await page.evaluate(() => window.__pwRenderFrameCount || 0);
  await page.waitForFunction(
    ({ start, delta }) => (window.__pwRenderFrameCount || 0) >= start + delta,
    { start: initialCount, delta: frameDelta },
    { timeout: timeoutMs },
  );
}

async function waitForGameLoad(page) {
  const timeoutMs = 60_000;
  const pollMs = 500;
  const diagnosticsEveryMs = 5_000;
  const startTime = Date.now();
  let lastDiagnosticsMs = -diagnosticsEveryMs;
  let latestState = null;

  while (Date.now() - startTime < timeoutMs) {
    latestState = await page.evaluate(() => {
      const loading = document.getElementById('loading');
      const loadingText = document.getElementById('loading-text');
      return {
        exists: Boolean(loading),
        hidden: Boolean(loading && loading.classList.contains('hidden')),
        className: loading ? loading.className : null,
        text: loadingText ? loadingText.textContent : null,
      };
    });

    if (latestState.hidden) {
      await ensureRenderTicker(page);
      await waitForRenderedFrames(page, 3, 5_000);
      return;
    }

    const elapsedMs = Date.now() - startTime;
    if (elapsedMs - lastDiagnosticsMs >= diagnosticsEveryMs) {
      console.log(
        `[waitForGameLoad] still waiting (${elapsedMs}ms): ${JSON.stringify(latestState)}`,
      );
      lastDiagnosticsMs = elapsedMs;
    }

    await page.waitForTimeout(pollMs);
  }

  throw new Error(
    `Timed out waiting for game load after ${timeoutMs}ms; last #loading state: ${JSON.stringify(latestState)}`,
  );
}

async function focusCanvas(page) {
  await page.locator('#canvas').click();
  await page.waitForFunction(
    () => document.activeElement && document.activeElement.id === 'canvas',
  );
}

async function waitForGameplayProgress(page, timeoutMs = 2_000) {
  const baseline = await page.evaluate(() => ({
    tick: window.__opengladLatestRenderSample?.tick ?? 0,
    renderSampleSeq: window.__opengladLatestRenderSample?.render_sample_seq ?? 0,
  }));

  await page.waitForFunction(
    ({ tick, renderSampleSeq }) => {
      const sample = window.__opengladLatestRenderSample;
      return Boolean(
        sample &&
          (sample.tick > tick || sample.render_sample_seq > renderSampleSeq + 1),
      );
    },
    baseline,
    { timeout: timeoutMs },
  );
}

async function clickCanvasGameCoord(page, gameX, gameY, holdMs = 150) {
  const canvas = page.locator('#canvas');
  const box = await canvas.boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }

  const cssX = box.x + (gameX * box.width) / 320;
  const cssY = box.y + (gameY * box.height) / 200;
  await page.mouse.move(cssX, cssY);
  await page.mouse.down();
  await page.waitForTimeout(holdMs);
  await page.mouse.up();
}

async function startSeededSinglePlayerFromPicker(page, options = {}) {
  const {
    preStartSettlingMs = 5_000,
    expectedCaptureProfileId = null,
    continueButton = { x: 150, y: 85 },
    goButton = { x: 250, y: 107 },
  } = options;

  await page.waitForFunction(
    () =>
      window.__opengladGameState === 1 || document.title === 'Gladiator',
    null,
    {
      timeout: 30_000,
    },
  );
  await waitForRenderedFrames(page, 4);
  await page.waitForTimeout(preStartSettlingMs);
  await clickCanvasGameCoord(page, continueButton.x, continueButton.y);
  await page.waitForTimeout(1_000);
  await clickCanvasGameCoord(page, goButton.x, goButton.y);
  await page.waitForTimeout(1_000);

  await page.waitForFunction(
    () =>
      window.__opengladGameState === 2 ||
      Boolean(window.__opengladLatestRenderSample),
    null,
    {
      timeout: 15_000,
    },
  );
  await waitForRenderedFrames(page, 4);

  let appliedProfile = null;

  if (expectedCaptureProfileId) {
    await page.waitForFunction(
      (profileId) =>
        window.__opengladAppliedJitterCaptureProfile &&
        window.__opengladAppliedJitterCaptureProfile.profile_id === profileId,
      expectedCaptureProfileId,
      { timeout: 15_000 },
    );
    appliedProfile = await page.evaluate(
      () => window.__opengladAppliedJitterCaptureProfile,
    );
  }

  await focusCanvas(page);
  try {
    await waitForGameplayProgress(page, 2_000);
  } catch (error) {
    await page.keyboard.press('Escape');
    await waitForGameplayProgress(page, 15_000);
    await waitForRenderedFrames(page, 4);
  }

  return appliedProfile;
}

module.exports = {
  clickCanvasGameCoord,
  ensureRenderTicker,
  focusCanvas,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
  waitForRenderedFrames,
};
