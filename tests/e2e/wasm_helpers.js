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

async function waitForGameplayRenderSamples(page, sampleDelta, timeoutMs) {
  const initialSeq = await page.evaluate(
    () => window.__opengladLatestRenderSample?.render_sample_seq ?? 0,
  );
  const startMs = Date.now();
  await page.waitForFunction(
    ({ start, delta }) =>
      (window.__opengladLatestRenderSample?.render_sample_seq ?? 0) >=
      start + delta,
    { start: initialSeq, delta: sampleDelta },
    { timeout: timeoutMs },
  );
  return Date.now() - startMs;
}

async function clickCanvasGameCoord(page, gameX, gameY, holdMs = 150) {
  const canvas = page.locator('#canvas');
  const box = await canvas.boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }

  const ui = fitUiReferenceGrid(box);
  const cssX = ui.x + (gameX * ui.width) / 320;
  const cssY = ui.y + (gameY * ui.height) / 200;
  await page.mouse.move(cssX, cssY);
  await page.mouse.down();
  await page.waitForTimeout(holdMs);
  await page.mouse.up();
}

async function movePointerOffCanvasBox(page, box) {
  const viewport = page.viewportSize();
  if (!viewport) {
    throw new Error('Viewport size is unavailable');
  }

  const candidates = [
    { x: 1, y: 1 },
    { x: viewport.width - 2, y: 1 },
    { x: 1, y: viewport.height - 2 },
    { x: viewport.width - 2, y: viewport.height - 2 },
  ];
  const point = candidates.find(
    ({ x, y }) =>
      x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height,
  );
  // The portrait/full-viewport shell intentionally leaves no point outside
  // the canvas. Its top-left pixel is inert in every fixed UI, so use it as
  // the hover-clearing fallback.
  const target = point || { x: box.x + 1, y: box.y + 1 };

  await page.mouse.move(target.x, target.y);
  // Give the SDL picker a poll cycle to clear any hovered button state.
  await page.waitForTimeout(100);
}

function fitUiReferenceGrid(box) {
  let width = box.width;
  let height = box.height;
  if (box.width * 200 > box.height * 320) {
    width = Math.floor((box.height * 320) / 200);
  } else {
    height = Math.floor((box.width * 200) / 320);
  }
  return {
    x: box.x + Math.floor((box.width - width) / 2),
    y: box.y + Math.floor((box.height - height) / 2),
    width,
    height,
  };
}

async function getCanvasUiBox(page) {
  const box = await page.locator('#canvas').boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }
  return fitUiReferenceGrid(box);
}

// Capture a sub-rectangle addressed on the classic 320x200 UI grid. The
// outer canvas now fills arbitrary viewport aspects; the engine letterboxes
// fixed menus/dialogs into this same centered rectangle.
async function getCanvasGameRegionScreenshot(page, gameX, gameY, gameW, gameH) {
  const box = await page.locator('#canvas').boundingBox();
  if (!box) {
    throw new Error('Canvas bounding box is unavailable');
  }
  await movePointerOffCanvasBox(page, box);
  const ui = fitUiReferenceGrid(box);
  return await page.screenshot({
    clip: {
      x: ui.x + (gameX * ui.width) / 320,
      y: ui.y + (gameY * ui.height) / 200,
      width: (gameW * ui.width) / 320,
      height: (gameH * ui.height) / 200,
    },
  });
}

// The picker runs inside picker_init() under Asyncify, so __opengladGameState
// stays null while it is up; the document title flipping to 'Gladiator' is the
// reliable readiness signal. fadeblack() then eats events for a while, hence
// the generous settling delay.
async function waitForPickerReady(page, settlingMs = 5_000) {
  await page.waitForFunction(
    () => window.__opengladGameState === 1 || document.title === 'Gladiator',
    null,
    { timeout: 30_000 },
  );
  await waitForRenderedFrames(page, 4);
  await page.waitForTimeout(settlingMs);
}

// From the picker main menu: CONTINUE opens the team-build menu.
async function continueToTeamBuildMenu(page) {
  await waitForPickerReady(page);
  await clickCanvasGameCoord(page, 150, 85);
  await page.waitForTimeout(1_500);
}

// From the team-build menu: open the NETWORKING menu.
async function openNetworkingFromTeamBuild(page) {
  await clickCanvasGameCoord(page, 250, 150);
  await page.waitForTimeout(1_500);
}

async function navigateToNetworkingMenu(page) {
  await continueToTeamBuildMenu(page);
  await openNetworkingFromTeamBuild(page);
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
    // Backspace is the web back key (physical Escape is swallowed by the
    // engine on web builds), so it is the key that can unstick a menu here.
    await page.keyboard.press('Backspace');
    await waitForGameplayProgress(page, 15_000);
    await waitForRenderedFrames(page, 4);
  }

  return appliedProfile;
}

module.exports = {
  clickCanvasGameCoord,
  continueToTeamBuildMenu,
  ensureRenderTicker,
  focusCanvas,
  getCanvasGameRegionScreenshot,
  getCanvasUiBox,
  navigateToNetworkingMenu,
  openNetworkingFromTeamBuild,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
  waitForGameplayProgress,
  waitForGameplayRenderSamples,
  waitForPickerReady,
  waitForRenderedFrames,
};
