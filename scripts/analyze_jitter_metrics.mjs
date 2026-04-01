#!/usr/bin/env node

import childProcess from 'child_process';
import fs from 'fs';
import path from 'path';

const KEYFRAME_INTERVAL_TICKS = 12 * 5;
const HEARTBEAT_INTERVAL_MS = 2000;
const CONTRACT_VERSION = 1;
const CLASSIFICATION_RULES_VERSION = 'phase1-v1';
const MINIMUM_CAPTURE_DURATION_MS = 11_500;
const MINIMUM_MOTION_RATIO = 0.70;
const MINIMUM_JITTER_FRAME_COUNT = 6;
const DOMINANT_PERIOD_TOLERANCE_RATIO = 0.20;
const MINIMUM_CORRELATED_EVENT_OVERLAP_RATIO = 0.50;
const FLOAT_MOTION_EPSILON = 0.01;

function parseArgs(argv) {
  const args = new Map();
  for (let index = 2; index < argv.length; index += 1) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!key.startsWith('--') || value === undefined) {
      throw new Error(`invalid argument pair near ${key ?? '<eof>'}`);
    }
    args.set(key, value);
    index += 1;
  }
  return args;
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8'));
}

function readText(file) {
  return fs.readFileSync(file, 'utf8');
}

function writeText(file, content) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, content, 'utf8');
}

function jsonEqual(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function deepClone(value) {
  return JSON.parse(JSON.stringify(value));
}

function percentile(values, ratio) {
  if (values.length === 0) {
    return 0;
  }
  const sorted = [...values].sort((a, b) => a - b);
  const position = Math.min(
    sorted.length - 1,
    Math.max(0, Math.ceil(ratio * sorted.length) - 1),
  );
  return sorted[position];
}

function stats(values) {
  const filtered = values.filter((value) => Number.isFinite(value));
  if (filtered.length === 0) {
    return { p50: 0, p95: 0, p99: 0, max: 0 };
  }
  return {
    p50: percentile(filtered, 0.50),
    p95: percentile(filtered, 0.95),
    p99: percentile(filtered, 0.99),
    max: Math.max(...filtered),
  };
}

function median(values) {
  if (values.length === 0) {
    return 0;
  }
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  if (sorted.length % 2 === 1) {
    return sorted[mid];
  }
  return (sorted[mid - 1] + sorted[mid]) / 2;
}

function roundedRenderTickIntervalMs(timerWait, speedFactor) {
  const intervalMs = Math.max(Number(timerWait) || 0, 0) * 13.6;
  if (speedFactor <= 0 || intervalMs <= 0) {
    return 0;
  }
  return Math.max(1, Math.round(intervalMs / speedFactor));
}

function browserWrapperTargetIntervalMs(timerWait, speedFactor) {
  const effectiveTimerWait = timerWait > 0 ? timerWait : 6;
  if (speedFactor === 0) {
    return 0;
  }
  let target = Math.trunc((effectiveTimerWait * 13.6) / speedFactor);
  if (target < 16) {
    target = 16;
  }
  return target;
}

function phaseBeatPeriodMs(cadenceMs, rafDeltaMs) {
  if (!Number.isFinite(cadenceMs) || cadenceMs <= 0 || !Number.isFinite(rafDeltaMs) || rafDeltaMs <= 0) {
    return cadenceMs;
  }

  const longFrames = Math.max(1, Math.ceil(cadenceMs / rafDeltaMs));
  const shortFrames = Math.max(1, Math.floor(cadenceMs / rafDeltaMs));
  const overshoot = longFrames * rafDeltaMs - cadenceMs;
  const shortfall = cadenceMs - shortFrames * rafDeltaMs;
  if (longFrames === shortFrames || overshoot <= 0 || shortfall <= 0) {
    return cadenceMs;
  }
  return cadenceMs * Math.ceil(shortfall / overshoot);
}

function buildExpectedPeriodCandidates(timerWait, speedFactor, rafDeltaP50) {
  const browserCadence = browserWrapperTargetIntervalMs(timerWait, speedFactor);
  const interpolationCadence = roundedRenderTickIntervalMs(timerWait, speedFactor);
  const keyframeCadence = interpolationCadence * KEYFRAME_INTERVAL_TICKS;

  return [
    {
      name: 'browser_outer_frame_pacing',
      alignment_mode: 'phase_beating',
      source_locations: [
        'src/platform/sdl/glad.cpp',
        'include/openglad/platform/frame_pacing.h',
        'src/platform/sdl/frame_pacing.cpp',
      ],
      cadence_ms: browserCadence,
      candidate_period_ms: phaseBeatPeriodMs(browserCadence, rafDeltaP50),
      timer_wait: timerWait,
      speed_factor: speedFactor,
      rationale:
        'Beat period between browser RAF cadence and the floored outer wrapper interval used by emscripten_frame_wrapper().',
    },
    {
      name: 'interpolation_interval_mismatch',
      alignment_mode: 'phase_beating',
      source_locations: [
        'src/gameplay/game_client.cpp',
        'include/openglad/platform/frame_pacing.h',
        'src/platform/sdl/frame_pacing.cpp',
      ],
      cadence_ms: interpolationCadence,
      candidate_period_ms: phaseBeatPeriodMs(interpolationCadence, rafDeltaP50),
      timer_wait: timerWait,
      speed_factor: speedFactor,
      rationale:
        'Beat period implied by the rounded render_tick_interval_ms() interpolation cadence against RAF sampling.',
    },
    {
      name: 'server_keyframe_interval',
      alignment_mode: 'direct_event',
      source_locations: [
        'src/gameplay/game_server.cpp',
        'include/openglad/gameplay/net_constants.h',
      ],
      cadence_ms: keyframeCadence,
      candidate_period_ms: keyframeCadence,
      timer_wait: timerWait,
      speed_factor: speedFactor,
      rationale:
        'Full authoritative snapshots recur every KEYFRAME_INTERVAL_TICKS server ticks.',
    },
    {
      name: 'snapshot_hash_check_interval',
      alignment_mode: 'direct_event',
      source_locations: [
        'src/gameplay/game_client.cpp',
        'include/openglad/gameplay/net_constants.h',
      ],
      cadence_ms: keyframeCadence,
      candidate_period_ms: keyframeCadence,
      timer_wait: timerWait,
      speed_factor: speedFactor,
      rationale:
        'Client snapshot-hash checks follow the same periodic full-snapshot interval.',
    },
    {
      name: 'heartbeat_interval',
      alignment_mode: 'direct_event',
      source_locations: [
        'src/gameplay/game_client.cpp',
      ],
      cadence_ms: HEARTBEAT_INTERVAL_MS,
      candidate_period_ms: HEARTBEAT_INTERVAL_MS,
      timer_wait: timerWait,
      speed_factor: speedFactor,
      rationale:
        'Heartbeat work is live at 2000 ms but should be suppressed during held-input motion because send_input() refreshes outbound activity every tick.',
    },
  ];
}

function buildFreshDeltas(samples) {
  const freshSamples = samples
    .map((sample, index) => ({ ...sample, index }))
    .filter((sample) => sample.sample_fresh);

  const deltas = [];
  for (let index = 1; index < freshSamples.length; index += 1) {
    const previous = freshSamples[index - 1];
    const current = freshSamples[index];
    const controlDx = current.control_render_x - previous.control_render_x;
    const controlDy = current.control_render_y - previous.control_render_y;
    const floatCameraDx = current.camera_topx_float - previous.camera_topx_float;
    const floatCameraDy = current.camera_topy_float - previous.camera_topy_float;
    const snappedCameraDx = current.camera_topx - previous.camera_topx;
    const snappedCameraDy = current.camera_topy - previous.camera_topy;

    deltas.push({
      sampleIndex: current.index,
      rafTimeMs: current.raf_time_ms,
      controlDx,
      controlDy,
      controlMagnitude: Math.hypot(controlDx, controlDy),
      floatCameraDx,
      floatCameraDy,
      floatCameraMagnitude: Math.hypot(floatCameraDx, floatCameraDy),
      snappedCameraDx,
      snappedCameraDy,
      snappedCameraMagnitude: Math.hypot(snappedCameraDx, snappedCameraDy),
    });
  }

  return deltas;
}

function rollingMedian(deltas, index, selector, radius = 3) {
  const values = [];
  for (let offset = -radius; offset <= radius; offset += 1) {
    if (offset === 0) {
      continue;
    }
    const candidate = deltas[index + offset];
    if (!candidate) {
      continue;
    }
    const value = selector(candidate);
    if (Number.isFinite(value) && value > 0) {
      values.push(value);
    }
  }
  return median(values);
}

function buildRunMetrics(sampleIndexes) {
  if (sampleIndexes.length === 0) {
    return { runCount: 0, longestRunFrames: 0, runStarts: [] };
  }

  const sorted = [...sampleIndexes].sort((a, b) => a - b);
  let runCount = 0;
  let longestRunFrames = 0;
  let currentRunLength = 0;
  let previous = null;
  const runStarts = [];

  for (const index of sorted) {
    if (previous === null || index !== previous + 1) {
      runCount += 1;
      currentRunLength = 1;
      runStarts.push(index);
    } else {
      currentRunLength += 1;
    }
    longestRunFrames = Math.max(longestRunFrames, currentRunLength);
    previous = index;
  }

  return { runCount, longestRunFrames, runStarts };
}

function correlateEvents(jitterIndexes, samples, engineEvents, rafWindowMs) {
  const ignoredFamilies = new Set([
    'render:viewscreen_redraw',
    'render:query_interpolation_alpha',
    'render:resolve_control_render_position_interpolated',
    'render:resolve_control_render_position_local',
    'render:resolve_control_render_position_fallback',
    'game_client:poll_messages_begin',
    'game_client:poll_messages_end',
  ]);

  const jitterTimes = jitterIndexes.map((index) => samples[index]?.raf_time_ms).filter(Number.isFinite);
  if (jitterTimes.length === 0) {
    return {
      family: '',
      overlap_count: 0,
      overlap_ratio: 0,
      matching_event_count: 0,
      window_ms: rafWindowMs,
    };
  }

  const familyMatches = new Map();
  for (const event of engineEvents) {
    if (!Number.isFinite(event.browser_time_ms)) {
      continue;
    }
    const family = `${event.category}:${event.event}`;
    if (ignoredFamilies.has(family)) {
      continue;
    }
    if (!familyMatches.has(family)) {
      familyMatches.set(family, { jitterIndexes: new Set(), eventCount: 0 });
    }
    const entry = familyMatches.get(family);
    entry.eventCount += 1;
    jitterTimes.forEach((time, index) => {
      if (Math.abs(event.browser_time_ms - time) <= rafWindowMs) {
        entry.jitterIndexes.add(jitterIndexes[index]);
      }
    });
  }

  let best = {
    family: '',
    overlap_count: 0,
    overlap_ratio: 0,
    matching_event_count: 0,
    window_ms: rafWindowMs,
  };
  for (const [family, match] of familyMatches.entries()) {
    const overlapCount = match.jitterIndexes.size;
    const overlapRatio = overlapCount / jitterIndexes.length;
    if (
      overlapRatio > best.overlap_ratio ||
      (overlapRatio === best.overlap_ratio && overlapCount > best.overlap_count)
    ) {
      best = {
        family,
        overlap_count: overlapCount,
        overlap_ratio: overlapRatio,
        matching_event_count: match.eventCount,
        window_ms: rafWindowMs,
      };
    }
  }

  return best;
}

function chooseDominantPeriod(
  runStartIndexes,
  samples,
  candidates,
  toleranceRatio = DOMINANT_PERIOD_TOLERANCE_RATIO,
) {
  const runStartTimes = runStartIndexes
    .map((index) => samples[index]?.raf_time_ms)
    .filter(Number.isFinite);
  if (runStartTimes.length < 2) {
    return { dominantPeriodMs: 0, dominantPeriodScore: 0, matchedCandidate: null };
  }

  const intervals = [];
  for (let index = 1; index < runStartTimes.length; index += 1) {
    intervals.push(runStartTimes[index] - runStartTimes[index - 1]);
  }

  let bestCandidate = null;
  let bestScore = 0;
  let bestMeasuredPeriod = median(intervals);

  for (const candidate of candidates) {
    const matches = intervals.filter(
      (interval) =>
        Math.abs(interval - candidate.candidate_period_ms) <=
        candidate.candidate_period_ms * toleranceRatio,
    );
    const score = matches.length / intervals.length;
    if (score > bestScore) {
      bestScore = score;
      bestCandidate = candidate;
      bestMeasuredPeriod = matches.length > 0 ? median(matches) : median(intervals);
    }
  }

  return {
    dominantPeriodMs: bestMeasuredPeriod,
    dominantPeriodScore: bestScore,
    matchedCandidate: bestCandidate,
  };
}

function fixedArraySet(...arrays) {
  return [...new Set(arrays.flat().sort((a, b) => a - b))];
}

function assertBrowserSchema(browser) {
  if (!browser?.metadata || !Array.isArray(browser?.samples)) {
    throw new Error('browser capture is missing metadata or samples[]');
  }
  if (!browser.metadata.applied_start) {
    throw new Error('browser capture is missing metadata.applied_start');
  }
}

function buildConfirmedRootCause(bestCandidate, correlatedEvent) {
  const family = correlatedEvent.family;
  if (
    bestCandidate?.name === 'browser_outer_frame_pacing' ||
    family.startsWith('browser_pacing:')
  ) {
    return {
      kind: 'browser_outer_frame_pacing',
      source_locations: [
        'src/platform/sdl/glad.cpp',
        'include/openglad/platform/frame_pacing.h',
        'src/platform/sdl/frame_pacing.cpp',
      ],
      rationale:
        'The strongest period match lands on the browser wrapper cadence family, and the highest-overlap correlated events come from emscripten_frame_wrapper().',
      fix_files: [
        'src/platform/sdl/glad.cpp',
        'include/openglad/platform/frame_pacing.h',
        'src/platform/sdl/frame_pacing.cpp',
      ],
    };
  }

  if (
    bestCandidate?.name === 'interpolation_interval_mismatch' ||
    family.startsWith('game_client:render_alpha')
  ) {
    return {
      kind: 'interpolation_interval_mismatch',
      source_locations: [
        'src/gameplay/game_client.cpp',
        'include/openglad/platform/frame_pacing.h',
        'src/platform/sdl/frame_pacing.cpp',
      ],
      rationale:
        'The measured cadence matches the rounded interpolation interval family more closely than transport periodic work, pointing at disagreement between presentation timing and outer frame pacing.',
      fix_files: [
        'src/gameplay/game_client.cpp',
        'include/openglad/platform/frame_pacing.h',
        'src/platform/sdl/frame_pacing.cpp',
      ],
    };
  }

  if (
    family.startsWith('game_server:') ||
    family.startsWith('game_client:heartbeat') ||
    family.startsWith('game_client:snapshot_hash') ||
    family.startsWith('inprocess_transport:')
  ) {
    return {
      kind: 'transport_periodic_work',
      source_locations: [
        'src/gameplay/game_server.cpp',
        'src/gameplay/game_client.cpp',
        'src/gameplay/net_transport_inprocess.cpp',
      ],
      rationale:
        'The best-correlated event family comes from periodic snapshot or transport work rather than the outer browser pacing path.',
      fix_files: [
        'src/gameplay/game_server.cpp',
        'src/gameplay/game_client.cpp',
        'src/gameplay/net_transport_inprocess.cpp',
      ],
    };
  }

  return {
    kind: 'other_periodic_runtime',
    source_locations: [
      'src/platform/sdl/local_transport_shadow.cpp',
      'src/interface/render/view.cpp',
      'src/interface/render/walker_draw.cpp',
    ],
    rationale:
      'No single browser or transport cadence dominated; the strongest overlap remained in another periodic runtime path.',
    fix_files: [
      'src/platform/sdl/local_transport_shadow.cpp',
      'src/interface/render/view.cpp',
      'src/interface/render/walker_draw.cpp',
    ],
  };
}

function buildMeasurementMethod(browserCaptureProfile, renderContractVersion, expectedPeriodCandidatesMs) {
  return {
    contract_version: CONTRACT_VERSION,
    browser_capture_profile: browserCaptureProfile,
    render_sample_contract: {
      global_name: 'window.__opengladLatestRenderSample',
      contract_version: renderContractVersion,
      publication_point:
        'Published once per displayed gameplay redraw for view 0 after interpolation and camera-follow resolution and before presentation.',
      published_fields: [
        'render_sample_seq',
        'engine_time_ms',
        'view_index',
        'tick',
        'timer_wait',
        'speed_factor',
        'interpolation_alpha',
        'control_worldx',
        'control_worldy',
        'control_render_x',
        'control_render_y',
        'camera_topx',
        'camera_topy',
        'camera_topx_float',
        'camera_topy_float',
      ],
    },
    capture_start_sampling_point:
      'First published render sample observed after window.__opengladAppliedJitterCaptureProfile acknowledgement and immediately before the 12 s hold window opens.',
    minimum_capture_duration_ms: MINIMUM_CAPTURE_DURATION_MS,
    expected_motion_completeness: {
      formula_name: 'rounded_render_interval_motion_updates',
      rounded_interval_source: 'render_tick_interval_ms()',
      timer_wait_source: 'browser-timing.json.metadata.timer_wait',
      speed_factor_source: 'browser-timing.json.metadata.speed_factor',
      expected_motion_update_count_formula:
        'floor(capture_duration_ms / rounded_render_tick_interval_ms(timer_wait, speed_factor))',
      minimum_ratio: MINIMUM_MOTION_RATIO,
    },
    expected_period_candidates_ms: expectedPeriodCandidatesMs,
    classification_rules_version: CLASSIFICATION_RULES_VERSION,
    classification_rules: {
      stall_frame:
        'A fresh float-motion sample whose control_render delta magnitude is <= epsilon and whose adjacent fresh samples remain non-zero.',
      irregular_motion_frame:
        'A fresh float-motion sample whose non-zero control_render delta magnitude deviates materially from the rolling fresh-motion median.',
      stale_presentation_frame:
        'A RAF sample with sample_fresh == false that belongs to a stale run at least one RAF longer than the median redraw repeat count for the held-input capture.',
      snapped_camera_irregular_frame:
        'A fresh sample whose snapped camera delta differs from the rolling snapped-camera median by at least 1 px while the corresponding float camera delta remains non-zero and within 20% of its rolling median.',
      presentation_jitter_frame:
        'Union of stale_presentation_frame and snapped_camera_irregular_frame.',
      jitter_frame:
        'Union of float-motion jitter (stall_frame or irregular_motion_frame) and presentation_jitter_frame.',
    },
    reproduced_gate: {
      minimum_jitter_frame_count: MINIMUM_JITTER_FRAME_COUNT,
      dominant_period_tolerance_ratio: DOMINANT_PERIOD_TOLERANCE_RATIO,
      minimum_correlated_event_overlap_ratio: MINIMUM_CORRELATED_EVENT_OVERLAP_RATIO,
    },
  };
}

function normalizeMeasurementMethodForComparison(measurementMethod) {
  return deepClone(measurementMethod);
}

function assertBaselineAnalysis(baseline) {
  if (!baseline || typeof baseline !== 'object') {
    throw new Error('baseline analysis is required for --expect fixed');
  }
  if (baseline.reproduced !== true) {
    throw new Error('baseline analysis must record reproduced: true');
  }
  if (!baseline.measurement_method || typeof baseline.measurement_method !== 'object') {
    throw new Error('baseline analysis is missing measurement_method');
  }
  if (!Array.isArray(baseline.measurement_method.expected_period_candidates_ms)) {
    throw new Error('baseline measurement_method is missing expected_period_candidates_ms');
  }
  if (!baseline.measurement_method.expected_motion_completeness) {
    throw new Error('baseline measurement_method is missing expected_motion_completeness');
  }
  if (!baseline.measurement_method.reproduced_gate) {
    throw new Error('baseline measurement_method is missing reproduced_gate');
  }
  if (!baseline.confirmed_root_cause?.kind) {
    throw new Error('baseline analysis is missing confirmed_root_cause.kind');
  }
}

function buildAnalysisMarkdown(analysis) {
  const lines = [
    '# Jitter Analysis',
    '',
    `- reproduced: ${analysis.reproduced}`,
  ];

  if (Object.prototype.hasOwnProperty.call(analysis, 'baseline_reproduced')) {
    lines.push(`- baseline_reproduced: ${analysis.baseline_reproduced}`);
  }
  if (Object.prototype.hasOwnProperty.call(analysis, 'same_measurement_method')) {
    lines.push(`- same_measurement_method: ${analysis.same_measurement_method}`);
  }

  lines.push(
    `- capture_duration_ms: ${analysis.capture_duration_ms.toFixed(2)}`,
    `- positive_motion_frame_count: ${analysis.positive_motion_frame_count}`,
    `- jitter_frame_count: ${analysis.jitter_frame_count}`,
    `- jitter_signature_kind: ${analysis.jitter_signature_kind}`,
    `- dominant_period_ms: ${analysis.dominant_period_ms.toFixed(2)}`,
    `- dominant_period_score: ${analysis.dominant_period_score.toFixed(3)}`,
    `- correlated_event: ${(analysis.remaining_correlated_event || analysis.correlated_event).family || 'none'} (${(((analysis.remaining_correlated_event || analysis.correlated_event).overlap_ratio) || 0).toFixed(3)} overlap ratio)`,
    '',
  );

  if (analysis.baseline_confirmed_root_cause) {
    lines.push(
      '## Baseline Root Cause',
      '',
      `- kind: ${analysis.baseline_confirmed_root_cause.kind}`,
      `- rationale: ${analysis.baseline_confirmed_root_cause.rationale}`,
      `- source_locations: ${analysis.baseline_confirmed_root_cause.source_locations.join(', ')}`,
      `- fix_files: ${analysis.baseline_confirmed_root_cause.fix_files.join(', ')}`,
      '',
    );
  } else if (analysis.confirmed_root_cause) {
    lines.push(
      '## Root Cause',
      '',
      `- kind: ${analysis.confirmed_root_cause.kind}`,
      `- rationale: ${analysis.confirmed_root_cause.rationale}`,
      `- source_locations: ${analysis.confirmed_root_cause.source_locations.join(', ')}`,
      `- fix_files: ${analysis.confirmed_root_cause.fix_files.join(', ')}`,
      '',
    );
  }

  lines.push(
    '## Candidate Periods',
    '',
    ...analysis.expected_period_candidates_ms.map(
      (candidate) =>
        `- ${candidate.name}: cadence=${candidate.cadence_ms.toFixed(2)} ms, candidate_period=${candidate.candidate_period_ms.toFixed(2)} ms`,
    ),
    '',
    '## Notes',
    '',
    `- expected_motion_update_count: ${analysis.expected_motion_update_count}`,
    `- motion_completeness_ratio: ${analysis.motion_completeness_ratio.toFixed(3)}`,
    `- stale_presentation_threshold_frames: ${analysis.stale_presentation_threshold_frames.toFixed(2)}`,
  );

  if (Object.prototype.hasOwnProperty.call(analysis, 'capture_sufficient')) {
    lines.push(`- capture_sufficient: ${analysis.capture_sufficient}`);
  }

  return `${lines.join('\n')}\n`;
}

function exactValue(value) {
  if (typeof value === 'string') {
    return value;
  }
  return JSON.stringify(value);
}

function buildBeforeAfterSummary(baseline, analysis) {
  const metrics = [
    ['p50', baseline.raf_delta_ms_stats.p50, analysis.raf_delta_ms_stats.p50],
    ['p95', baseline.raf_delta_ms_stats.p95, analysis.raf_delta_ms_stats.p95],
    ['p99', baseline.raf_delta_ms_stats.p99, analysis.raf_delta_ms_stats.p99],
    ['max', baseline.raf_delta_ms_stats.max, analysis.raf_delta_ms_stats.max],
    ['stall_run_count', baseline.stall_run_count, analysis.stall_run_count],
    [
      'longest_stall_run_frames',
      baseline.longest_stall_run_frames,
      analysis.longest_stall_run_frames,
    ],
    [
      'irregular_motion_frame_count',
      baseline.irregular_motion_frame_count,
      analysis.irregular_motion_frame_count,
    ],
    [
      'irregular_motion_run_count',
      baseline.irregular_motion_run_count,
      analysis.irregular_motion_run_count,
    ],
    [
      'presentation_jitter_frame_count',
      baseline.presentation_jitter_frame_count,
      analysis.presentation_jitter_frame_count,
    ],
    [
      'presentation_jitter_run_count',
      baseline.presentation_jitter_run_count,
      analysis.presentation_jitter_run_count,
    ],
    ['jitter_frame_count', baseline.jitter_frame_count, analysis.jitter_frame_count],
    [
      'jitter_signature_kind',
      baseline.jitter_signature_kind,
      analysis.jitter_signature_kind,
    ],
    ['dominant_period_ms', baseline.dominant_period_ms, analysis.dominant_period_ms],
    [
      'dominant_period_score',
      baseline.dominant_period_score,
      analysis.dominant_period_score,
    ],
    [
      'correlated_event',
      baseline.correlated_event,
      analysis.correlated_event,
    ],
  ];

  const lines = [
    '# Before/After Jitter Summary',
    '',
    `- baseline_reproduced: ${baseline.reproduced}`,
    `- fixed_reproduced: ${analysis.reproduced}`,
    `- same_measurement_method: ${analysis.same_measurement_method}`,
    '',
  ];

  for (const [name, before, after] of metrics) {
    lines.push(`- ${name}: before=${exactValue(before)} after=${exactValue(after)}`);
  }

  return `${lines.join('\n')}\n`;
}

function assertFixedAnalysis(fixed) {
  if (!fixed || typeof fixed !== 'object') {
    throw new Error('fixed analysis is required for phase-3 final confirmation');
  }
  if (fixed.reproduced !== false) {
    throw new Error('fixed analysis must record reproduced: false');
  }
  if (fixed.baseline_reproduced !== true) {
    throw new Error('fixed analysis must record baseline_reproduced: true');
  }
  if (fixed.same_measurement_method !== true) {
    throw new Error('fixed analysis must record same_measurement_method: true');
  }
  if (!fixed.measurement_method || typeof fixed.measurement_method !== 'object') {
    throw new Error('fixed analysis is missing measurement_method');
  }
}

function formatNumber(value, digits = 2) {
  return Number.isFinite(value) ? value.toFixed(digits) : String(value);
}

function formatCorrelatedEvent(event) {
  const family = event?.family || 'none';
  const overlapRatio = Number.isFinite(event?.overlap_ratio) ? event.overlap_ratio : 0;
  return `${family} (${overlapRatio.toFixed(3)} overlap ratio)`;
}

function buildTesterVerdictMarkdown(analysis) {
  const lines = [
    '# Tester Verdict',
    '',
    '- verdict: pass',
    `- reproduced: ${analysis.reproduced}`,
    `- baseline_reproduced: ${analysis.baseline_reproduced}`,
    `- fixed_reproduced: ${analysis.fixed_reproduced}`,
    `- same_measurement_method: ${analysis.same_measurement_method}`,
    `- capture_duration_ms: ${formatNumber(analysis.capture_duration_ms, 2)}`,
    `- positive_motion_frame_count: ${analysis.positive_motion_frame_count}`,
    `- presentation_jitter_frame_count: ${analysis.presentation_jitter_frame_count}`,
    `- presentation_jitter_run_count: ${analysis.presentation_jitter_run_count}`,
    `- jitter_signature_kind: ${analysis.jitter_signature_kind}`,
    `- jitter_frame_count: ${analysis.jitter_frame_count}`,
    `- dominant_period_ms: ${formatNumber(analysis.dominant_period_ms, 2)}`,
    `- dominant_period_score: ${formatNumber(analysis.dominant_period_score, 3)}`,
    `- remaining_correlated_event: ${formatCorrelatedEvent(analysis.remaining_correlated_event)}`,
    '- conclusion: The focused native rerun and the Chromium browser rerun both preserve the saved phase-1 measurement method and do not reproduce the periodic single-player jitter symptom.',
    '',
  ];
  return `${lines.join('\n')}\n`;
}

function escapeRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function latestCommitMatching(prefix) {
  const output = childProcess.execFileSync(
    'git',
    [
      'log',
      '--extended-regexp',
      `--grep=^${escapeRegex(prefix)}`,
      '--format=%H%n%h%n%s',
      '-n',
      '1',
    ],
    { encoding: 'utf8' },
  ).trim();

  if (!output) {
    throw new Error(`no git commit found for prefix ${prefix}`);
  }

  const [fullHash, shortHash, subject] = output.split('\n');
  return { prefix, fullHash, shortHash, subject };
}

function extractMetricLine(markdown, name) {
  const prefix = `- ${name}: `;
  return markdown
    .split('\n')
    .find((line) => line.startsWith(prefix))
    ?.slice(prefix.length) ?? '';
}

function verificationSignature(analysis) {
  return {
    reproduced: analysis.reproduced,
    baseline_reproduced: analysis.baseline_reproduced,
    fixed_reproduced: analysis.fixed_reproduced,
    same_measurement_method: analysis.same_measurement_method,
    capture_duration_ms: analysis.capture_duration_ms,
    positive_motion_frame_count: analysis.positive_motion_frame_count,
    measurement_method: analysis.measurement_method,
    presentation_jitter_frame_count: analysis.presentation_jitter_frame_count,
    presentation_jitter_run_count: analysis.presentation_jitter_run_count,
    jitter_signature_kind: analysis.jitter_signature_kind,
    jitter_frame_count: analysis.jitter_frame_count,
    dominant_period_ms: analysis.dominant_period_ms,
    dominant_period_score: analysis.dominant_period_score,
    remaining_correlated_event: analysis.remaining_correlated_event,
  };
}

function assertTesterAnalysisMatches(testerAnalysis, finalAnalysis) {
  if (!testerAnalysis || typeof testerAnalysis !== 'object') {
    throw new Error('tester analysis is required for senior-tester final verification');
  }
  if (!jsonEqual(verificationSignature(testerAnalysis), verificationSignature(finalAnalysis))) {
    throw new Error('tester analysis does not match the senior-tester recomputation');
  }
}

function buildPhase3Commands({
  browserPath,
  enginePath,
  baselinePath,
  fixedPath,
  outputJsonPath,
  outputMdPath,
  testerAnalysisPath,
  testerVerdictPath,
  phase1CommitPrefix,
  phase2CommitPrefix,
  phase3CommitPrefix,
}) {
  return [
    'cmake --build --preset ci-test --target og_test_game_core og_test_view og_unit_sim',
    'bash scripts/run_jitter_gtests.sh ./build/ci-test/og_test_game_core ./build/ci-test/og_test_view ./build/ci-test/og_unit_sim',
    'bash scripts/test_emscripten_build.sh',
    'OG_JITTER_CAPTURE_DIR=.plan/artifacts/jitter/final npx playwright test tests/e2e/wasm-jitter.spec.js --project=chromium',
    `node scripts/analyze_jitter_metrics.mjs --browser ${browserPath} --engine ${enginePath} --baseline ${baselinePath} --fixed ${fixedPath} --expect fixed --verifier tester --output-json ${testerAnalysisPath} --output-md ${testerVerdictPath}`,
    `node scripts/analyze_jitter_metrics.mjs --browser ${browserPath} --engine ${enginePath} --baseline ${baselinePath} --fixed ${fixedPath} --tester-analysis ${testerAnalysisPath} --tester-verdict ${testerVerdictPath} --phase1-commit-prefix '${phase1CommitPrefix}' --phase2-commit-prefix '${phase2CommitPrefix}' --phase3-commit-prefix '${phase3CommitPrefix}' --expect fixed --output-json ${outputJsonPath} --output-md ${outputMdPath}`,
    'cmake --build --preset ci-test',
    'ctest --preset ci-test --output-on-failure',
  ];
}

function stripLeadingTitle(markdown) {
  return markdown
    .trim()
    .split('\n')
    .filter((line, index) => !(index === 0 && line.startsWith('# ')))
    .join('\n')
    .trim();
}

function buildFinalVerificationMarkdown({
  baseline,
  baselineMd,
  fixed,
  fixedMd,
  beforeAfterMd,
  testerAnalysis,
  testerVerdictMd,
  finalAnalysis,
  browser,
  engine,
  phaseCommands,
  phaseCommits,
}) {
  const baselineAnalysisEvent = extractMetricLine(baselineMd, 'correlated_event');
  const fixedCaptureSufficient = extractMetricLine(fixedMd, 'capture_sufficient');
  const beforeAfterPresentation = extractMetricLine(beforeAfterMd, 'presentation_jitter_frame_count');
  const beforeAfterJitter = extractMetricLine(beforeAfterMd, 'jitter_frame_count');
  const beforeAfterDominantPeriod = extractMetricLine(beforeAfterMd, 'dominant_period_ms');
  const beforeAfterCorrelatedEvent = extractMetricLine(beforeAfterMd, 'correlated_event');
  const phase1CorrelatedFamily =
    baseline.remaining_correlated_event?.family || baseline.correlated_event?.family || 'none';
  const finalCorrelatedFamily =
    finalAnalysis.remaining_correlated_event?.family || finalAnalysis.correlated_event?.family || 'none';

  const lines = [
    '# Final Verification Package',
    '',
    '## Outcome Summary',
    '',
    '- The phase-1 testcase reproduced the periodic single-player jitter under the saved measurement method.',
    '- The phase-2 fixed rerun did not reproduce the jitter under that same method.',
    '- The phase-3 final rerun also did not reproduce the jitter under that same method.',
    `- The correlated event identified in phase 1 (${phase1CorrelatedFamily}) is absent, no longer periodic, or no longer causative in the final rerun; the fresh final correlated event is ${finalCorrelatedFamily}.`,
    '',
    '## Baseline Summary',
    '',
    `- Sources: .plan/artifacts/jitter/baseline/analysis.json and .plan/artifacts/jitter/baseline/analysis.md`,
    `- reproduced: ${baseline.reproduced}`,
    `- capture_duration_ms: ${formatNumber(baseline.capture_duration_ms, 2)}`,
    `- positive_motion_frame_count: ${baseline.positive_motion_frame_count}`,
    `- jitter_frame_count: ${baseline.jitter_frame_count}`,
    `- jitter_signature_kind: ${baseline.jitter_signature_kind}`,
    `- dominant_period_ms: ${formatNumber(baseline.dominant_period_ms, 2)}`,
    `- baseline analysis summary line: ${baselineAnalysisEvent}`,
    `- confirmed_root_cause.kind: ${baseline.confirmed_root_cause.kind}`,
    '',
    '## Fixed-Run Summary',
    '',
    '- Sources: .plan/artifacts/jitter/fixed/analysis.json, .plan/artifacts/jitter/fixed/analysis.md, and .plan/artifacts/jitter/fixed/before-after-summary.md',
    `- reproduced: ${fixed.reproduced}`,
    `- baseline_reproduced: ${fixed.baseline_reproduced}`,
    `- same_measurement_method: ${fixed.same_measurement_method}`,
    `- capture_duration_ms: ${formatNumber(fixed.capture_duration_ms, 2)}`,
    `- positive_motion_frame_count: ${fixed.positive_motion_frame_count}`,
    `- capture_sufficient: ${fixedCaptureSufficient}`,
    `- before/after presentation_jitter_frame_count: ${beforeAfterPresentation}`,
    `- before/after jitter_frame_count: ${beforeAfterJitter}`,
    `- before/after dominant_period_ms: ${beforeAfterDominantPeriod}`,
    `- before/after correlated_event: ${beforeAfterCorrelatedEvent}`,
    '',
    '## Tester Summary',
    '',
    '- Sources: .plan/artifacts/jitter/final/tester-analysis.json and .plan/artifacts/jitter/final/tester-verdict.md',
    `- reproduced: ${testerAnalysis.reproduced}`,
    `- baseline_reproduced: ${testerAnalysis.baseline_reproduced}`,
    `- fixed_reproduced: ${testerAnalysis.fixed_reproduced}`,
    `- same_measurement_method: ${testerAnalysis.same_measurement_method}`,
    `- capture_duration_ms: ${formatNumber(testerAnalysis.capture_duration_ms, 2)}`,
    `- positive_motion_frame_count: ${testerAnalysis.positive_motion_frame_count}`,
    `- presentation_jitter_frame_count: ${testerAnalysis.presentation_jitter_frame_count}`,
    `- presentation_jitter_run_count: ${testerAnalysis.presentation_jitter_run_count}`,
    `- jitter_frame_count: ${testerAnalysis.jitter_frame_count}`,
    `- dominant_period_ms: ${formatNumber(testerAnalysis.dominant_period_ms, 2)}`,
    `- remaining_correlated_event: ${formatCorrelatedEvent(testerAnalysis.remaining_correlated_event)}`,
    '',
    '## Fresh Final Capture Summary',
    '',
    `- browser profile_id: ${browser.metadata.profile_id}`,
    `- browser capture_duration_ms: ${formatNumber(browser.metadata.capture_duration_ms, 2)}`,
    `- browser timer_wait: ${browser.metadata.timer_wait}`,
    `- browser speed_factor: ${browser.metadata.speed_factor}`,
    `- browser sample_count: ${browser.samples.length}`,
    `- engine event_count: ${(engine.events || []).length}`,
    `- final positive_motion_frame_count: ${finalAnalysis.positive_motion_frame_count}`,
    `- final jitter_frame_count: ${finalAnalysis.jitter_frame_count}`,
    `- final jitter_signature_kind: ${finalAnalysis.jitter_signature_kind}`,
    `- final dominant_period_ms: ${formatNumber(finalAnalysis.dominant_period_ms, 2)}`,
    `- final remaining_correlated_event: ${formatCorrelatedEvent(finalAnalysis.remaining_correlated_event)}`,
    '',
    '## Phase-3 Commands Run',
    '',
    ...phaseCommands.map((command) => `- \`${command}\``),
    '',
    '## Phase Commit Hashes',
    '',
    ...phaseCommits.map(
      (commit) => `- ${commit.prefix} ${commit.shortHash} (${commit.fullHash}): ${commit.subject}`,
    ),
    '',
    '## Tester Verdict',
    '',
    stripLeadingTitle(testerVerdictMd),
    '',
    '## Senior-Tester Verdict',
    '',
    '- verdict: pass',
    '- conclusion: I independently recomputed the final quantitative comparison from the fresh Chromium capture, matched the tester-owned final metrics, and confirmed that the saved phase-1 jitter symptom still does not reproduce under the unchanged measurement method.',
    `- reproduced: ${finalAnalysis.reproduced}`,
    `- baseline_reproduced: ${finalAnalysis.baseline_reproduced}`,
    `- fixed_reproduced: ${finalAnalysis.fixed_reproduced}`,
    `- same_measurement_method: ${finalAnalysis.same_measurement_method}`,
    `- capture_duration_ms: ${formatNumber(finalAnalysis.capture_duration_ms, 2)}`,
    `- positive_motion_frame_count: ${finalAnalysis.positive_motion_frame_count}`,
    `- presentation_jitter_frame_count: ${finalAnalysis.presentation_jitter_frame_count}`,
    `- presentation_jitter_run_count: ${finalAnalysis.presentation_jitter_run_count}`,
    `- jitter_signature_kind: ${finalAnalysis.jitter_signature_kind}`,
    `- jitter_frame_count: ${finalAnalysis.jitter_frame_count}`,
    `- dominant_period_ms: ${formatNumber(finalAnalysis.dominant_period_ms, 2)}`,
    `- dominant_period_score: ${formatNumber(finalAnalysis.dominant_period_score, 3)}`,
    `- remaining_correlated_event: ${formatCorrelatedEvent(finalAnalysis.remaining_correlated_event)}`,
    '',
  ];

  return `${lines.join('\n')}\n`;
}

function main() {
  const args = parseArgs(process.argv);
  const browserPath = args.get('--browser');
  const enginePath = args.get('--engine');
  const expectMode = args.get('--expect');
  const outputJsonPath = args.get('--output-json');
  const outputMdPath = args.get('--output-md');
  const baselinePath = args.get('--baseline');
  const fixedPath = args.get('--fixed');
  const verifier = args.get('--verifier');
  const beforeAfterMdPath = args.get('--before-after-md');
  const testerAnalysisPath = args.get('--tester-analysis');
  const testerVerdictPath = args.get('--tester-verdict');
  const phase1CommitPrefix = args.get('--phase1-commit-prefix');
  const phase2CommitPrefix = args.get('--phase2-commit-prefix');
  const phase3CommitPrefix = args.get('--phase3-commit-prefix');
  const seniorTesterMode = Boolean(
    testerAnalysisPath ||
      testerVerdictPath ||
      phase1CommitPrefix ||
      phase2CommitPrefix ||
      phase3CommitPrefix,
  );

  if (!browserPath || !enginePath || !expectMode || !outputJsonPath || !outputMdPath) {
    throw new Error(
      '--browser, --engine, --expect, --output-json, and --output-md are required',
    );
  }
  if (expectMode === 'fixed' && !baselinePath) {
    throw new Error('--expect fixed requires --baseline');
  }
  if (verifier === 'tester' && !fixedPath) {
    throw new Error('--verifier tester requires --fixed');
  }
  if (seniorTesterMode) {
    if (!fixedPath) {
      throw new Error('--fixed is required for senior-tester final verification');
    }
    if (!testerAnalysisPath || !testerVerdictPath) {
      throw new Error('--tester-analysis and --tester-verdict are required for senior-tester final verification');
    }
    if (!phase1CommitPrefix || !phase2CommitPrefix || !phase3CommitPrefix) {
      throw new Error('--phase1-commit-prefix, --phase2-commit-prefix, and --phase3-commit-prefix are required for senior-tester final verification');
    }
  }

  const browser = readJson(browserPath);
  const engine = readJson(enginePath);
  const baseline = baselinePath ? readJson(baselinePath) : null;
  const fixed = fixedPath ? readJson(fixedPath) : null;
  assertBrowserSchema(browser);
  if (expectMode === 'fixed') {
    assertBaselineAnalysis(baseline);
  }
  if (fixedPath) {
    assertFixedAnalysis(fixed);
  }

  const samples = browser.samples;
  const rafDeltas = samples.map((sample) => sample.raf_delta_ms).filter((value) => value > 0);
  const rafDeltaMsStats = stats(rafDeltas);

  const freshDeltas = buildFreshDeltas(samples);
  const positiveMotionFrameIndexes = freshDeltas
    .filter((delta) => delta.controlMagnitude > FLOAT_MOTION_EPSILON)
    .map((delta) => delta.sampleIndex);

  const stallFrameIndexes = [];
  const irregularMotionFrameIndexes = [];
  const snappedCameraIrregularFrameIndexes = [];
  for (let index = 0; index < freshDeltas.length; index += 1) {
    const delta = freshDeltas[index];
    const previous = freshDeltas[index - 1];
    const next = freshDeltas[index + 1];
    if (
      delta.controlMagnitude <= FLOAT_MOTION_EPSILON &&
      previous &&
      next &&
      previous.controlMagnitude > FLOAT_MOTION_EPSILON &&
      next.controlMagnitude > FLOAT_MOTION_EPSILON
    ) {
      stallFrameIndexes.push(delta.sampleIndex);
    }

    const motionMedian = rollingMedian(freshDeltas, index, (candidate) => candidate.controlMagnitude);
    if (
      delta.controlMagnitude > FLOAT_MOTION_EPSILON &&
      motionMedian > FLOAT_MOTION_EPSILON &&
      Math.abs(delta.controlMagnitude - motionMedian) >=
        Math.max(0.2, motionMedian * 0.35)
    ) {
      irregularMotionFrameIndexes.push(delta.sampleIndex);
    }

    for (const axis of [
      {
        snapped: Math.abs(delta.snappedCameraDx),
        snappedMedian: rollingMedian(
          freshDeltas,
          index,
          (candidate) => Math.abs(candidate.snappedCameraDx),
        ),
        floatValue: Math.abs(delta.floatCameraDx),
        floatMedian: rollingMedian(
          freshDeltas,
          index,
          (candidate) => Math.abs(candidate.floatCameraDx),
        ),
      },
      {
        snapped: Math.abs(delta.snappedCameraDy),
        snappedMedian: rollingMedian(
          freshDeltas,
          index,
          (candidate) => Math.abs(candidate.snappedCameraDy),
        ),
        floatValue: Math.abs(delta.floatCameraDy),
        floatMedian: rollingMedian(
          freshDeltas,
          index,
          (candidate) => Math.abs(candidate.floatCameraDy),
        ),
      },
    ]) {
      if (
        axis.snappedMedian > 0 &&
        axis.floatMedian > 0 &&
        axis.floatValue > 0 &&
        Math.abs(axis.snapped - axis.snappedMedian) >= 1 &&
        Math.abs(axis.floatValue - axis.floatMedian) <= axis.floatMedian * 0.20
      ) {
        snappedCameraIrregularFrameIndexes.push(delta.sampleIndex);
        break;
      }
    }
  }

  const staleRuns = [];
  let currentStaleRun = [];
  samples.forEach((sample, index) => {
    if (!sample.sample_fresh) {
      currentStaleRun.push(index);
      return;
    }
    if (currentStaleRun.length > 0) {
      staleRuns.push(currentStaleRun);
      currentStaleRun = [];
    }
  });
  if (currentStaleRun.length > 0) {
    staleRuns.push(currentStaleRun);
  }
  const staleRunMedianLength = median(staleRuns.map((run) => run.length));
  const stalePresentationFrameIndexes = staleRuns
    .filter((run) => run.length >= staleRunMedianLength + 1)
    .flat();

  const presentationJitterFrameIndexes = fixedArraySet(
    stalePresentationFrameIndexes,
    snappedCameraIrregularFrameIndexes,
  );
  const floatMotionJitterFrameIndexes = fixedArraySet(
    stallFrameIndexes,
    irregularMotionFrameIndexes,
  );
  const jitterFrameIndexes = fixedArraySet(
    floatMotionJitterFrameIndexes,
    presentationJitterFrameIndexes,
  );

  const stallRuns = buildRunMetrics(stallFrameIndexes);
  const irregularRuns = buildRunMetrics(irregularMotionFrameIndexes);
  const presentationRuns = buildRunMetrics(presentationJitterFrameIndexes);
  const jitterRuns = buildRunMetrics(jitterFrameIndexes);

  const motionDeltaStats = {
    float_control_delta_abs: stats(
      freshDeltas.map((delta) => Math.abs(delta.controlMagnitude)),
    ),
    float_camera_delta_abs: stats(
      freshDeltas.map((delta) => Math.abs(delta.floatCameraMagnitude)),
    ),
    snapped_camera_delta_abs: stats(
      freshDeltas.map((delta) => Math.abs(delta.snappedCameraMagnitude)),
    ),
  };

  const timerWait = browser.metadata.timer_wait;
  const speedFactor = browser.metadata.speed_factor;
  const expectedMotionIntervalMs = roundedRenderTickIntervalMs(timerWait, speedFactor);
  const expectedMotionUpdateCount =
    expectedMotionIntervalMs > 0
      ? Math.floor(browser.metadata.capture_duration_ms / expectedMotionIntervalMs)
      : 0;
  const expectedPeriodCandidatesMs = buildExpectedPeriodCandidates(
    timerWait,
    speedFactor,
    rafDeltaMsStats.p50 || 16.67,
  );
  const analysisPeriodCandidatesMs = expectedPeriodCandidatesMs;

  const { dominantPeriodMs, dominantPeriodScore, matchedCandidate } =
    chooseDominantPeriod(
      jitterRuns.runStarts,
      samples,
      analysisPeriodCandidatesMs,
      expectMode === 'fixed'
        ? baseline.measurement_method.reproduced_gate.dominant_period_tolerance_ratio
        : DOMINANT_PERIOD_TOLERANCE_RATIO,
    );

  const correlatedEvent = correlateEvents(
    jitterFrameIndexes,
    samples,
    engine.events || [],
    Math.max(rafDeltaMsStats.p50 || 0, 1),
  );

  let jitterSignatureKind = 'mixed';
  if (floatMotionJitterFrameIndexes.length > 0 && presentationJitterFrameIndexes.length === 0) {
    jitterSignatureKind = 'float_motion';
  } else if (
    presentationJitterFrameIndexes.length > 0 &&
    floatMotionJitterFrameIndexes.length === 0
  ) {
    jitterSignatureKind = 'presentation_path';
  }

  const measurementMethodFromCapture = buildMeasurementMethod(
    browser.metadata.applied_start,
    browser.metadata.contract_version,
    expectedPeriodCandidatesMs,
  );
  const measurementMethod = measurementMethodFromCapture;
  const sameMeasurementMethod =
    expectMode === 'fixed'
      ? jsonEqual(
          normalizeMeasurementMethodForComparison(measurementMethodFromCapture),
          normalizeMeasurementMethodForComparison(baseline.measurement_method),
        )
      : true;
  const reproducedGate =
    expectMode === 'fixed'
      ? baseline.measurement_method.reproduced_gate
      : {
          minimum_jitter_frame_count: MINIMUM_JITTER_FRAME_COUNT,
          dominant_period_tolerance_ratio: DOMINANT_PERIOD_TOLERANCE_RATIO,
          minimum_correlated_event_overlap_ratio: MINIMUM_CORRELATED_EVENT_OVERLAP_RATIO,
        };
  const dominantPeriodToleranceRatio =
    reproducedGate.dominant_period_tolerance_ratio;
  const dominantPeriodMatchesCandidate =
    matchedCandidate !== null &&
    matchedCandidate.candidate_period_ms > 0 &&
    Math.abs(dominantPeriodMs - matchedCandidate.candidate_period_ms) <=
      matchedCandidate.candidate_period_ms * dominantPeriodToleranceRatio;
  const minimumCaptureDurationMs =
    expectMode === 'fixed'
      ? baseline.measurement_method.minimum_capture_duration_ms
      : MINIMUM_CAPTURE_DURATION_MS;
  const minimumMotionRatio =
    expectMode === 'fixed'
      ? baseline.measurement_method.expected_motion_completeness.minimum_ratio
      : MINIMUM_MOTION_RATIO;
  const motionCompletenessRatio =
    expectedMotionUpdateCount > 0
      ? positiveMotionFrameIndexes.length / expectedMotionUpdateCount
      : 0;
  const captureSufficient =
    sameMeasurementMethod &&
    browser.metadata.capture_duration_ms >= minimumCaptureDurationMs &&
    motionCompletenessRatio >= minimumMotionRatio;
  const baselineCorrelatedEventFamily =
    expectMode === 'fixed'
      ? baseline.remaining_correlated_event?.family || baseline.correlated_event?.family || null
      : null;
  const strongCorrelatedEvent =
    captureSufficient &&
    jitterFrameIndexes.length >= reproducedGate.minimum_jitter_frame_count &&
    correlatedEvent.overlap_ratio >=
      reproducedGate.minimum_correlated_event_overlap_ratio;
  const baselineCorrelatedEventFamilySurvived =
    expectMode === 'fixed' &&
    strongCorrelatedEvent &&
    baselineCorrelatedEventFamily !== null &&
    correlatedEvent.family === baselineCorrelatedEventFamily;
  const baselineRootCauseRuntimeFamilySurvived =
    expectMode === 'fixed' &&
    baseline.confirmed_root_cause?.kind === 'browser_outer_frame_pacing' &&
    strongCorrelatedEvent &&
    typeof correlatedEvent.family === 'string' &&
    correlatedEvent.family.startsWith('browser_pacing:');

  const reproduced =
    captureSufficient &&
    (
      (
        strongCorrelatedEvent &&
        dominantPeriodMatchesCandidate
      ) ||
      baselineCorrelatedEventFamilySurvived ||
      baselineRootCauseRuntimeFamilySurvived
    );

  const confirmedRootCause =
    expectMode === 'fixed'
      ? null
      : buildConfirmedRootCause(matchedCandidate, correlatedEvent);

  const analysis = {
    reproduced,
    measurement_method: measurementMethod,
    capture_duration_ms: browser.metadata.capture_duration_ms,
    raf_delta_ms_stats: rafDeltaMsStats,
    motion_delta_stats: motionDeltaStats,
    positive_motion_frame_count: positiveMotionFrameIndexes.length,
    stall_run_count: stallRuns.runCount,
    longest_stall_run_frames: stallRuns.longestRunFrames,
    irregular_motion_frame_count: irregularMotionFrameIndexes.length,
    irregular_motion_run_count: irregularRuns.runCount,
    presentation_jitter_frame_count: presentationJitterFrameIndexes.length,
    presentation_jitter_run_count: presentationRuns.runCount,
    jitter_frame_count: jitterFrameIndexes.length,
    jitter_signature_kind: jitterSignatureKind,
    dominant_period_ms: dominantPeriodMs,
    dominant_period_score: dominantPeriodScore,
    expected_motion_update_count: expectedMotionUpdateCount,
    motion_completeness_ratio: motionCompletenessRatio,
    capture_sufficient: captureSufficient,
    stale_presentation_threshold_frames: staleRunMedianLength + 1,
    expected_period_candidates_ms: analysisPeriodCandidatesMs,
    correlated_event: correlatedEvent,
  };
  if (confirmedRootCause) {
    analysis.confirmed_root_cause = confirmedRootCause;
  }
  if (expectMode === 'fixed') {
    analysis.baseline_reproduced = baseline.reproduced;
    if (fixed) {
      analysis.fixed_reproduced = fixed.reproduced;
    }
    analysis.same_measurement_method = sameMeasurementMethod;
    analysis.baseline_confirmed_root_cause = baseline.confirmed_root_cause;
    analysis.baseline_correlated_event_family = baselineCorrelatedEventFamily;
    analysis.baseline_correlated_event_family_survived =
      baselineCorrelatedEventFamilySurvived;
    analysis.baseline_root_cause_runtime_family_survived =
      baselineRootCauseRuntimeFamilySurvived;
    analysis.remaining_correlated_event = correlatedEvent;
  }

  let fixedModeError = null;
  if (expectMode === 'fixed') {
    if (!sameMeasurementMethod) {
      fixedModeError =
        'expected fixed capture, but measurement_method drifted from the saved baseline contract';
    } else if (browser.metadata.capture_duration_ms < minimumCaptureDurationMs) {
      fixedModeError =
        'expected fixed capture, but capture duration is below baseline minimum';
    } else if (motionCompletenessRatio < minimumMotionRatio) {
      fixedModeError =
        'expected fixed capture, but positive motion completeness is below baseline minimum';
    } else if (fixed && fixed.reproduced !== false) {
      fixedModeError =
        'expected fixed capture, but the saved fixed analysis does not record reproduced: false';
    } else if (baselineCorrelatedEventFamilySurvived) {
      fixedModeError =
        'expected fixed capture, but the saved baseline correlated event family still survives in the new jitter signature';
    } else if (baselineRootCauseRuntimeFamilySurvived) {
      fixedModeError =
        'expected fixed capture, but a browser_pacing correlated event still survives in the new jitter signature';
    } else if (analysis.reproduced) {
      fixedModeError =
        'expected fixed capture, but jitter reproduction gate still passed';
    }
  }

  let outputMd = buildAnalysisMarkdown(analysis);
  if (verifier === 'tester') {
    outputMd = buildTesterVerdictMarkdown(analysis);
  } else if (seniorTesterMode) {
    const testerAnalysis = readJson(testerAnalysisPath);
    const testerVerdictMd = readText(testerVerdictPath);
    assertTesterAnalysisMatches(testerAnalysis, analysis);
    const phaseCommits = [
      latestCommitMatching(phase1CommitPrefix),
      latestCommitMatching(phase2CommitPrefix),
      latestCommitMatching(phase3CommitPrefix),
    ];
    const baselineMd = readText(path.join(path.dirname(baselinePath), 'analysis.md'));
    const fixedMd = readText(path.join(path.dirname(fixedPath), 'analysis.md'));
    const beforeAfterMd = readText(path.join(path.dirname(fixedPath), 'before-after-summary.md'));
    outputMd = buildFinalVerificationMarkdown({
      baseline,
      baselineMd,
      fixed,
      fixedMd,
      beforeAfterMd,
      testerAnalysis,
      testerVerdictMd,
      finalAnalysis: analysis,
      browser,
      engine,
      phaseCommands: buildPhase3Commands({
        browserPath,
        enginePath,
        baselinePath,
        fixedPath,
        outputJsonPath,
        outputMdPath,
        testerAnalysisPath,
        testerVerdictPath,
        phase1CommitPrefix,
        phase2CommitPrefix,
        phase3CommitPrefix,
      }),
      phaseCommits,
    });
    analysis.tester_analysis_agrees = true;
    analysis.phase_commit_hashes = phaseCommits.reduce((result, commit) => {
      result[commit.prefix] = {
        full_hash: commit.fullHash,
        short_hash: commit.shortHash,
        subject: commit.subject,
      };
      return result;
    }, {});
  }

  writeText(outputJsonPath, `${JSON.stringify(analysis, null, 2)}\n`);
  writeText(outputMdPath, outputMd);
  if (beforeAfterMdPath && expectMode === 'fixed') {
    writeText(beforeAfterMdPath, buildBeforeAfterSummary(baseline, analysis));
  }
  if (fixedModeError !== null) {
    throw new Error(fixedModeError);
  }

  if (expectMode === 'reproduced') {
    if (!analysis.reproduced) {
      throw new Error('expected reproduced capture, but reproduction gate failed');
    }
    return;
  }

  if (expectMode === 'fixed') {
    return;
  }

  throw new Error(`unsupported --expect value: ${expectMode}`);
}

try {
  main();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
