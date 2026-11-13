// @ts-check
// The jitter gate's verdict rules, pinned on captures built here rather than
// on whatever cadence the runner delivers. No page is opened: the analyzer is
// a pure function of the two capture files, and tests/e2e/wasm-jitter.spec.js
// is the one that records them from a live build.
//
// Why this exists: on 2026-11-12 that live gate went red on a healthy build
// (GitHub run 43325303315). The runner delivered 43% of its frames a refresh
// late, the analyzer blamed seven of the frames right after those gaps on the
// engine, and three of the six intervals between them happened to fall near
// the 2 s heartbeat candidate. Each case below is one of the rules that
// verdict needed, with a capture that isolates it.
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');
const { test, expect } = require('@playwright/test');

const ANALYZER_SCRIPT = path.resolve(
  __dirname,
  '..',
  '..',
  'scripts',
  'analyze_jitter_metrics.mjs',
);

const REFRESH_MS = 16.7;
const PIXELS_PER_REFRESH = 2;

// A capture of `turns` requestAnimationFrame turns (their lengths in ms). The
// control and its camera move with the wall clock, PIXELS_PER_REFRESH per
// refresh, except where `stallAt` says the engine stalled: that turn shows no
// motion and the next one shows two turns' worth, the stall-and-catch-up pair
// the analyzer reads as one two-frame jitter run. With `catchUp` false the
// lost motion is never repaid and the run is the stalled frame alone. Every
// run gets the pacing event a real capture carries beside it.
function buildCapture(turns, stallAt, { catchUp = true } = {}) {
  const stalls = new Set(stallAt);
  const samples = [];
  const events = [];
  let time = 1000;
  let x = 336;
  let owed = 0;
  samples.push(sampleAt(0, time, 0, x));
  turns.forEach((turn, turnIndex) => {
    const index = turnIndex + 1;
    time += turn;
    const step = PIXELS_PER_REFRESH * Math.round(turn / REFRESH_MS);
    if (stalls.has(index)) {
      owed += catchUp ? step : 0;
      events.push({
        trace_seq: events.length,
        engine_time_ms: Math.round(time),
        browser_time_ms: time,
        category: 'browser_pacing',
        event: 'wrapper_frame_skip',
      });
    } else {
      x += step + owed;
      owed = 0;
    }
    samples.push(sampleAt(index, time, turn, x));
  });
  const appliedStart = {
    contract_version: 1,
    profile_id: 'single-player-right-run',
    preload_start_config: { scenario_id: 11, difficulty: 1 },
    input_hold: { player_index: 0, logical_action: 'move_right', playwright_key: 'd' },
  };
  return {
    browser: {
      metadata: {
        contract_version: 1,
        profile_id: 'single-player-right-run',
        scenario_id: 11,
        difficulty: 1,
        capture_duration_ms: time - 1000,
        capture_start_engine_time_ms: 1000,
        timer_wait: 6,
        speed_factor: 1,
        applied_start: appliedStart,
      },
      samples,
    },
    engine: {
      metadata: { contract_version: 1, profile_id: 'single-player-right-run' },
      events,
    },
  };
}

function sampleAt(index, time, turn, x) {
  return {
    raf_time_ms: time,
    raf_delta_ms: turn,
    sample_seq: index,
    render_sample_seq: index + 1,
    sample_fresh: true,
    engine_time_ms: Math.round(time),
    tick: Math.floor(time / 82),
    interpolation_alpha: 1,
    control_worldx: x,
    control_worldy: 864,
    control_render_x: x,
    control_render_y: 864,
    camera_topx: x - 170,
    camera_topy: 772,
    camera_topx_float: x - 170,
    camera_topy_float: 772,
  };
}

// The sample index that arrives closest to `ms` after the capture starts.
function turnAt(turns, ms) {
  let time = 0;
  let best = 1;
  let bestDistance = Infinity;
  turns.forEach((turn, turnIndex) => {
    time += turn;
    if (Math.abs(time - ms) < bestDistance) {
      bestDistance = Math.abs(time - ms);
      best = turnIndex + 1;
    }
  });
  return best;
}

function analyze(testInfo, capture) {
  const directory = testInfo.outputPath('jitter-capture');
  fs.mkdirSync(directory, { recursive: true });
  const browserPath = path.join(directory, 'browser-timing.json');
  const enginePath = path.join(directory, 'engine-timing.json');
  const analysisPath = path.join(directory, 'analysis.json');
  fs.writeFileSync(browserPath, JSON.stringify(capture.browser));
  fs.writeFileSync(enginePath, JSON.stringify(capture.engine));
  // --expect fixed is the one mode that writes its analysis and judges
  // nothing by exit code, so every verdict below is read from the file.
  const result = childProcess.spawnSync(
    process.execPath,
    [
      ANALYZER_SCRIPT,
      '--browser', browserPath,
      '--engine', enginePath,
      '--expect', 'not-reproduced',
      '--output-json', analysisPath,
      '--output-md', path.join(directory, 'analysis.md'),
    ],
    { encoding: 'utf8' },
  );
  const analysis = JSON.parse(fs.readFileSync(analysisPath, 'utf8'));
  return { analysis, exitCode: result.status, stderr: result.stderr };
}

const STEADY_60HZ = Array.from({ length: 720 }, () => REFRESH_MS);
// Where the seven leftover runs sat in the capture that went red, in ms.
const RED_RUN_TIMES_MS = [1633, 4017, 6383, 8966, 9600, 11433, 11516];

test('the pinned bug still reproduces: a stall on every 486 ms beat', async ({}, testInfo) => {
  const beats = [];
  for (let ms = 486; ms < 11_900; ms += 486) {
    beats.push(turnAt(STEADY_60HZ, ms));
  }
  const { analysis, exitCode, stderr } = analyze(testInfo, buildCapture(STEADY_60HZ, beats));
  expect(analysis.capture_sufficient).toBe(true);
  expect(analysis.engine_jitter_frame_count).toBe(2 * beats.length);
  expect(analysis.host_starved_jitter_frame_count).toBe(0);
  expect(analysis.dominant_period_match_count).toBe(beats.length - 1);
  expect(analysis.dominant_period_interval_count).toBe(beats.length - 1);
  expect(analysis.dominant_period_score).toBe(1);
  expect(analysis.dominant_period_chance).toBeLessThan(1e-12);
  expect(analysis.reproduced).toBe(true);
  expect(exitCode).toBe(1);
  expect(stderr).toContain('jitter reproduction gate passed');
});

test('a 2 s period reproduces on five agreeing intervals', async ({}, testInfo) => {
  const beats = [1000, 3000, 5000, 7000, 9000, 11_000].map((ms) => turnAt(STEADY_60HZ, ms));
  const { analysis } = analyze(testInfo, buildCapture(STEADY_60HZ, beats));
  expect(analysis.engine_jitter_frame_count).toBe(12);
  expect(analysis.dominant_period_match_count).toBe(5);
  expect(analysis.dominant_period_interval_count).toBe(5);
  expect(analysis.dominant_period_chance).toBeLessThan(0.01);
  expect(analysis.reproduced).toBe(true);
});

test('seven scattered runs are not a period, even on time and at score 0.5', async ({}, testInfo) => {
  // The red capture's run times on a flawless cadence, so every frame
  // testifies and only the period rule stands between them and a verdict.
  const runs = RED_RUN_TIMES_MS.map((ms) => turnAt(STEADY_60HZ, ms));
  const { analysis, exitCode } = analyze(testInfo, buildCapture(STEADY_60HZ, runs));
  expect(analysis.capture_sufficient).toBe(true);
  expect(analysis.engine_jitter_frame_count).toBe(14);
  expect(analysis.host_starved_jitter_frame_count).toBe(0);
  // Three of six intervals inside the heartbeat candidate's window: exactly
  // the score the old gate accepted.
  expect(analysis.dominant_period_match_count).toBe(3);
  expect(analysis.dominant_period_interval_count).toBe(6);
  expect(analysis.dominant_period_score).toBe(0.5);
  // ... and about one capture in six does that with no period at all.
  expect(analysis.dominant_period_chance).toBeGreaterThan(0.1);
  expect(analysis.dominant_period_chance).toBeLessThan(0.25);
  expect(analysis.reproduced).toBe(false);
  expect(exitCode).toBe(0);
});

test('two intervals are not a period, whatever they measure', async ({}, testInfo) => {
  // The trade-off the chance bound makes on purpose: a 12 s capture holds
  // three occurrences of a 4.92 s candidate at most.
  const runs = [1000, 5920, 10_840].map((ms) => turnAt(STEADY_60HZ, ms));
  const { analysis } = analyze(testInfo, buildCapture(STEADY_60HZ, runs));
  expect(analysis.engine_jitter_frame_count).toBe(6);
  expect(analysis.dominant_period_match_count).toBe(2);
  expect(analysis.dominant_period_interval_count).toBe(2);
  expect(analysis.dominant_period_score).toBe(1);
  expect(analysis.dominant_period_chance).toBeGreaterThan(0.01);
  expect(analysis.reproduced).toBe(false);
});

// One stalled frame on every beat of `periodMs`, on a cadence the two cases
// below then disturb one turn at a time.
function beatsOn(turns, periodMs, disturb) {
  const beats = [];
  for (let ms = periodMs / 2; ms < 11_400; ms += periodMs) {
    const index = turnAt(turns, ms);
    disturb(index);
    beats.push(index);
  }
  return beats;
}

test('a frame right after a late turn is the host catching up, not the engine', async ({}, testInfo) => {
  const onTime = [...STEADY_60HZ];
  const onTimeBeats = beatsOn(onTime, 486, () => {});
  const control = analyze(testInfo, buildCapture(onTime, onTimeBeats, { catchUp: false }));
  expect(control.analysis.engine_jitter_frame_count).toBe(onTimeBeats.length);
  expect(control.analysis.reproduced).toBe(true);

  // The same stalls, each now behind a turn the host delivered a refresh
  // late. That is what all seven blamed frames of the red capture were: on
  // time themselves, one turn after a gap.
  const late = [...STEADY_60HZ];
  const lateBeats = beatsOn(late, 486, (index) => {
    late[index - 2] = 2 * REFRESH_MS;  // the turn BEFORE the stalled one
  });
  const { analysis } = analyze(testInfo, buildCapture(late, lateBeats, { catchUp: false }));
  expect(analysis.capture_sufficient).toBe(true);
  expect(analysis.host_starvation_threshold_ms).toBeCloseTo(1.5 * REFRESH_MS, 5);
  expect(analysis.engine_jitter_frame_count).toBe(0);
  // The late frame (twice the motion) and the stalled frame behind it.
  expect(analysis.host_starved_jitter_frame_count).toBe(2 * lateBeats.length);
  expect(analysis.reproduced).toBe(false);
});

test('a short turn among skipped refreshes cannot testify either', async ({}, testInfo) => {
  // The cadence most hosted runners deliver: every second refresh. The beat
  // here is the 2 s heartbeat candidate, the one period that does not move
  // with the refresh the browser delivers.
  const skipping = Array.from({ length: 360 }, () => 2 * REFRESH_MS);
  const skippingBeats = beatsOn(skipping, 2000, () => {});
  expect(skippingBeats.length).toBe(6);
  const control = analyze(
    testInfo,
    buildCapture(skipping, skippingBeats, { catchUp: false }),
  );
  expect(control.analysis.engine_jitter_frame_count).toBe(skippingBeats.length);
  expect(control.analysis.reproduced).toBe(true);

  // The same stalls on turns that did get their refresh: half as long as
  // every turn around them, so half the motion and nothing to compare with.
  const mixed = Array.from({ length: 360 }, () => 2 * REFRESH_MS);
  const mixedBeats = beatsOn(mixed, 2000, (index) => {
    mixed[index - 1] = REFRESH_MS;  // the stalled frame's OWN turn
  });
  const { analysis } = analyze(testInfo, buildCapture(mixed, mixedBeats, { catchUp: false }));
  expect(analysis.capture_sufficient).toBe(true);
  expect(analysis.host_short_turn_threshold_ms).toBeCloseTo(1.5 * REFRESH_MS, 5);
  expect(analysis.engine_jitter_frame_count).toBe(0);
  expect(analysis.host_starved_jitter_frame_count).toBe(mixedBeats.length);
  expect(analysis.reproduced).toBe(false);
});
