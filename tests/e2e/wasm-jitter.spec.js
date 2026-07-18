// @ts-check
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');
const { test, expect } = require('@playwright/test');
const {
  focusCanvas,
  startSeededSinglePlayerFromPicker,
  waitForGameLoad,
} = require('./wasm_helpers');

const CAPTURE_DURATION_MS = 12_000;
const PROFILE_ID = 'single-player-right-run';
const ANALYZER_SCRIPT = path.resolve(
  __dirname,
  '..',
  '..',
  'scripts',
  'analyze_jitter_metrics.mjs',
);

test('captures browser timing and verifies jitter does not reproduce', async ({ page }, testInfo) => {
  test.setTimeout(120_000);

  const captureDir =
    process.env.OG_JITTER_CAPTURE_DIR ||
    path.join(testInfo.outputDir, 'jitter-capture');

  fs.mkdirSync(captureDir, { recursive: true });

  await page.addInitScript(() => {
    window.__opengladEnableRuntimeTraceCapture = true;
    window.__opengladSeedSinglePlayerTeam = true;
    window.__opengladSkipIntroForTests = true;
    window.__opengladJitterCaptureProfile = { profile_id: 'single-player-right-run' };
  });

  await page.goto('/play.html');
  await waitForGameLoad(page);

  const appliedStart = await startSeededSinglePlayerFromPicker(page, {
    expectedCaptureProfileId: PROFILE_ID,
  });
  expect(appliedStart).not.toBeNull();
  expect(appliedStart.profile_id).toBe(PROFILE_ID);

  await focusCanvas(page);

  // Capture with the whole app FULLSCREENED, matching the conditions this
  // gate's baseline was recorded under. History: the game used to request
  // browser fullscreen itself (SDL_WINDOW_FULLSCREEN_DESKTOP, deferred to
  // the first input), and every baseline/verdict for this gate was captured
  // that way. The engine no longer touches the Fullscreen API (user
  // decision: no auto-fullscreen), and that exposed a headless-compositor
  // artifact: swiftshader-backed headless chromium delivers rAF at ~30Hz
  // (33.3ms median deltas) to a windowed CSS-scaled canvas but the full
  // 60Hz to a fullscreened one — measured across CI runs 29154248607
  // (green, 16.7ms) vs 29166181661 (red, 33.3ms) with the engine publishing
  // a fresh frame on ~100% of rAF turns in BOTH. At 30Hz the analyzer's
  // snapped-camera cadence math (calibrated for the 72fps-on-60Hz beat)
  // flags every capture, halving the gate's sensitivity to the actual
  // engine bug it pins. Entering fullscreen from the test (trusted
  // gesture: focusCanvas just clicked) restores the baseline's 60Hz
  // measurement environment without re-introducing auto-fullscreen for
  // users. Real-GPU browsers deliver 60Hz either way (verified locally,
  // including under 4x CPU throttle).
  await page.getByRole('button', { name: 'Enter fullscreen' }).click();
  await page.waitForFunction(
    () => document.fullscreenElement === document.documentElement,
  );

  const startRenderSampleSeq = await page.evaluate(() => {
    window.__opengladRuntimeTraceEvents = [];
    return window.__opengladLatestRenderSample
      ? window.__opengladLatestRenderSample.render_sample_seq
      : 0;
  });

  await page.evaluate(({ captureDurationMs, startSeq, appliedStartContract }) => {
    const deepCopy = (value) => JSON.parse(JSON.stringify(value));
    window.__opengladPendingJitterCapture = { done: false, result: null };

    const samples = [];
    let sampleSeq = 0;
    let prevRafTime = null;
    let prevRenderSampleSeq = null;
    let captureStartSample = null;
    let captureStartRafTime = null;

    const collect = (rafTime) => {
      const latestSample = window.__opengladLatestRenderSample;
      if (!latestSample || typeof latestSample.render_sample_seq !== 'number') {
        window.requestAnimationFrame(collect);
        return;
      }

      if (captureStartSample === null) {
        if (latestSample.render_sample_seq === startSeq) {
          window.requestAnimationFrame(collect);
          return;
        }
        captureStartSample = deepCopy(latestSample);
        captureStartRafTime = rafTime;
      }

      const sample = deepCopy(window.__opengladLatestRenderSample);
      const renderSampleSeq = sample.render_sample_seq;
      const sampleFresh =
        prevRenderSampleSeq === null || renderSampleSeq !== prevRenderSampleSeq;

      samples.push({
        raf_time_ms: rafTime,
        raf_delta_ms: prevRafTime === null ? 0 : rafTime - prevRafTime,
        sample_seq: sampleSeq,
        render_sample_seq: renderSampleSeq,
        sample_fresh: sampleFresh,
        engine_time_ms: sample.engine_time_ms,
        tick: sample.tick,
        interpolation_alpha: sample.interpolation_alpha,
        control_worldx: sample.control_worldx,
        control_worldy: sample.control_worldy,
        control_render_x: sample.control_render_x,
        control_render_y: sample.control_render_y,
        camera_topx: sample.camera_topx,
        camera_topy: sample.camera_topy,
        camera_topx_float: sample.camera_topx_float,
        camera_topy_float: sample.camera_topy_float,
      });

      sampleSeq += 1;
      prevRafTime = rafTime;
      prevRenderSampleSeq = renderSampleSeq;

      if (rafTime - captureStartRafTime >= captureDurationMs) {
        window.__opengladPendingJitterCapture = {
          done: true,
          result: {
            metadata: {
              contract_version:
                captureStartSample.contract_version || appliedStartContract || 1,
              profile_id: window.__opengladAppliedJitterCaptureProfile.profile_id,
              scenario_id:
                window.__opengladAppliedJitterCaptureProfile.preload_start_config
                  .scenario_id,
              difficulty:
                window.__opengladAppliedJitterCaptureProfile.preload_start_config
                  .difficulty,
              capture_duration_ms: rafTime - captureStartRafTime,
              capture_start_engine_time_ms: captureStartSample.engine_time_ms,
              timer_wait: captureStartSample.timer_wait,
              speed_factor: captureStartSample.speed_factor,
              applied_start: deepCopy(window.__opengladAppliedJitterCaptureProfile),
            },
            samples,
          },
        };
        return;
      }

      window.requestAnimationFrame(collect);
    };

    window.requestAnimationFrame(collect);
  }, {
    captureDurationMs: CAPTURE_DURATION_MS,
    startSeq: startRenderSampleSeq,
    appliedStartContract: appliedStart.contract_version,
  });

  await page.keyboard.down(appliedStart.input_hold.playwright_key);
  await page.waitForFunction(
    () => Boolean(window.__opengladPendingJitterCapture?.done),
    null,
    { timeout: CAPTURE_DURATION_MS + 30_000 },
  );
  const browserTiming = await page.evaluate(
    () => window.__opengladPendingJitterCapture.result,
  );
  await page.keyboard.up(appliedStart.input_hold.playwright_key);

  const engineEvents = await page.evaluate(() => {
    const drained = window.__opengladRuntimeTraceEvents || [];
    window.__opengladRuntimeTraceEvents = [];
    return JSON.parse(JSON.stringify(drained));
  });

  expect(browserTiming.metadata.profile_id).toBe(PROFILE_ID);
  expect(browserTiming.metadata.capture_duration_ms).toBeGreaterThanOrEqual(11_500);
  expect(engineEvents.length).toBeGreaterThan(0);

  fs.writeFileSync(
    path.join(captureDir, 'browser-timing.json'),
    `${JSON.stringify(browserTiming, null, 2)}\n`,
  );
  fs.writeFileSync(
    path.join(captureDir, 'engine-timing.json'),
    `${JSON.stringify(
      {
        metadata: {
          contract_version: browserTiming.metadata.contract_version,
          profile_id: browserTiming.metadata.profile_id,
        },
        events: engineEvents,
      },
      null,
      2,
    )}\n`,
  );

  const analysisJsonPath = path.join(captureDir, 'analysis.json');
  const analysisMdPath = path.join(captureDir, 'analysis.md');
  try {
    childProcess.execFileSync(
      process.execPath,
      [
        ANALYZER_SCRIPT,
        '--browser',
        path.join(captureDir, 'browser-timing.json'),
        '--engine',
        path.join(captureDir, 'engine-timing.json'),
        '--expect',
        'not-reproduced',
        '--output-json',
        analysisJsonPath,
        '--output-md',
        analysisMdPath,
      ],
      { stdio: 'pipe' },
    );
  } catch (error) {
    const stderr = error && error.stderr ? String(error.stderr) : '';
    throw new Error(stderr.trim() || String(error));
  }

  const analysis = JSON.parse(fs.readFileSync(analysisJsonPath, 'utf8'));
  expect(analysis.capture_sufficient).toBe(true);
  expect(analysis.reproduced).toBe(false);
});
