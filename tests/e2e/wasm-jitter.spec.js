// @ts-check
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

test('captures browser and engine timing for jitter reproduction', async ({ page }, testInfo) => {
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
});
