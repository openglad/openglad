// @ts-check
const { test, expect } = require('@playwright/test');
const {
  clickCanvasGameCoord,
  continueToTeamBuildMenu,
  getCanvasGameRegionScreenshot,
  navigateToNetworkingMenu,
  openNetworkingFromTeamBuild,
  waitForGameLoad,
} = require('./wasm_helpers');
const { RelayStub } = require('./relay_stub');

// Regression coverage for the browser HOST/JOIN freeze: the web build used to
// run without C++ exception support, so the relay failure inside
// HostPickerLobbyClient::initialize_from_save() threw straight into abort()
// ("RuntimeError: Aborted(undefined)") and killed the wasm runtime. With
// -fexceptions the same failure must surface as a HOST GAME popup dialog and
// leave the picker fully interactive.

// Game-coordinate (320x200) button centers, verified against the live picker.
const NETWORKING_MENU_BUTTONS = {
  back: { x: 78, y: 175 },
  host: { x: 160, y: 175 },
  join: { x: 242, y: 175 },
};
const ACTIVE_ROOM_FIRST = { x: 160, y: 77 };
// §2.5/§2.10: the base-camp GO/READY slot is (244,178,68,18), center
// (278,187). On the host machine the rect reads GO (face color keyed to the
// §2.6 state table); on a joiner the same rect is the READY/UNREADY toggle
// twin, so the one coordinate drives both halves of the ready flow.
const GO_READY_BUTTON = { x: 278, y: 187 };
const GO_READY_SLOT_REGION = { x: 244, y: 178, w: 68, h: 18 };
// popup_dialog's single OK button: x 135..185, y 130..150. Its event loop
// exits on that click (or on a Return/Space/Backspace-as-back key press;
// physical Escape is swallowed on web).
const POPUP_OK_BUTTON = { x: 160, y: 140 };

// Screenshot regions on the 320x200 reference grid.
// "NETWORKING" headline (write_xy_center at y=26): present exactly while the
// networking menu is the active screen, so region equality against a baseline
// capture doubles as an "is this menu up and undarkened?" probe.
const NETWORKING_TITLE_REGION = { x: 60, y: 18, w: 200, h: 18 };
// popup_dialog darkens the screen and draws the dialog centered around y=80
// with the OK button below; any of that changes this region.
const POPUP_REGION = { x: 96, y: 50, w: 128, h: 104 };
// Team-build lobby status lines (picker_team_build.cpp): up to two lines at
// (12, 8) and (12, 18), e.g. "Direct: ..." / "Relay: GLAD-E2E1".
const TEAM_BUILD_STATUS_REGION = { x: 6, y: 2, w: 250, h: 28 };

// The shipped default relay (kDefaultRelayBaseUrl) is the LIVE deployed
// Cloudflare Worker, so "the default is unreachable" is no longer a given.
// Error-path tests explicitly override the relay with an RFC 6761 .invalid
// host, which is guaranteed to never resolve.
// The shipped default is the SAME-ORIGIN relay mount on the Pages site
// (routed to the relay Worker via a service binding).
const DEFAULT_RELAY_HOSTNAME = 'openglad.pages.dev';
const DEFAULT_RELAY_PATH_PREFIX = '/relay';
const LIVE_RELAY_BASE_URL =
  `https://${DEFAULT_RELAY_HOSTNAME}${DEFAULT_RELAY_PATH_PREFIX}`;
const UNREACHABLE_RELAY_BASE_URL = 'https://unreachable.invalid';

const IGNORED_RUNTIME_ERROR_PATTERNS = [
  /get_asset_path: readlink\(\/proc\/self\/exe\) failed/,
  // The error-path scenarios aim at the guaranteed-unreachable .invalid relay
  // override on purpose; Chromium reports the failed fetch (via the console
  // message's resource location) and the failed WebSocket connect as console
  // errors.
  /unreachable\.invalid/,
  // Incidental probes against the live default relay (e.g. the held-route
  // scenario's swallowed requests, or teardown-time socket closes in the
  // opt-in live test) also surface as console errors.
  /openglad-relay\.yans\.workers\.dev/,
];

function shouldIgnoreRuntimeError(message, extraIgnorePatterns = []) {
  // Never ignore a wasm abort, no matter what else the message mentions.
  if (/\bAborted\b/i.test(message)) {
    return false;
  }
  return (
    IGNORED_RUNTIME_ERROR_PATTERNS.some((pattern) => pattern.test(message)) ||
    extraIgnorePatterns.some((pattern) => pattern.test(message))
  );
}

// `extraIgnorePatterns` lets stub-backed tests ignore the browser's expected
// WebSocket/fetch failure chatter aimed at the local relay stub (its
// 127.0.0.1:<port> URL is only known at runtime).
function attachRuntimeErrorCollectors(page, errors, extraIgnorePatterns = []) {
  page.on('pageerror', (err) => {
    const message = `pageerror: ${err.message}`;
    if (!shouldIgnoreRuntimeError(message, extraIgnorePatterns)) {
      errors.push(message);
    }
  });
  page.on('console', (msg) => {
    if (msg.type() !== 'error') {
      return;
    }
    // Include the source location: network failures ("Failed to load
    // resource") carry the offending URL there, not in the text.
    const location = msg.location();
    const message = `console.error: ${msg.text()} (${location ? location.url : ''})`;
    if (!shouldIgnoreRuntimeError(message, extraIgnorePatterns)) {
      errors.push(message);
    }
  });
}

function assertNoRuntimeErrors(errors, context) {
  expect(errors, `Unexpected runtime errors during: ${context}`).toEqual([]);
}

// A dead wasm runtime does NOT stop requestAnimationFrame, so frame counters
// cannot detect the freeze. The reliable signals are the "Aborted" pageerror /
// console text and the non-modularized runtime's global ABORT flag.
async function expectNoWasmAbort(page, errors, context) {
  const abortFlag = await page.evaluate(() => Boolean(window.ABORT));
  expect(abortFlag, `wasm ABORT flag raised during: ${context}`).toBe(false);
  const abortMessages = errors.filter((line) => /\bAborted\b/i.test(line));
  expect(abortMessages, `wasm abort reported during: ${context}`).toEqual([]);
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

async function waitForRegionToLeave(page, region, baseline, message, timeoutMs = 20_000) {
  await expect
    .poll(async () => (await captureRegion(page, region)).equals(baseline), {
      message,
      timeout: timeoutMs,
    })
    .toBe(false);
}

async function waitForRegionToShow(page, region, baseline, message, timeoutMs = 20_000) {
  await expect
    .poll(async () => (await captureRegion(page, region)).equals(baseline), {
      message,
      timeout: timeoutMs,
    })
    .toBe(true);
}

// Capture a region only once it has stopped changing: two consecutive
// captures a few frames apart must be byte-identical. The picker redraws
// every frame from deterministic state, so a settled region is a reliable
// reference for later "the text really changed" comparisons.
async function captureSettledRegion(page, region, description, timeoutMs = 20_000) {
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

// Wait for a region to STABILIZE on content that differs from every given
// baseline. Plain left-the-baseline polling can pass on a transient
// mid-transition frame; status-text contract assertions need the region to
// actually stay on the new text.
async function waitForRegionToSettleAway(
  page,
  region,
  baselines,
  message,
  timeoutMs = 30_000,
) {
  await expect
    .poll(
      async () => {
        const first = await captureRegion(page, region);
        if (baselines.some((baseline) => first.equals(baseline))) {
          return false;
        }
        await page.waitForTimeout(250);
        const second = await captureRegion(page, region);
        return (
          second.equals(first) &&
          !baselines.some((baseline) => second.equals(baseline))
        );
      },
      { message, timeout: timeoutMs },
    )
    .toBe(true);
}

// popup_dialog blocks in its own event loop until the OK button is clicked
// (or Return/Space/Backspace is pressed). Click OK and wait for the
// networking menu to be redrawn; retry the click once in case the blocking
// loop missed the first press. Keyboard fallbacks are deliberately avoided:
// once the popup is gone, Return/Backspace would activate menu items instead.
async function dismissPopupAndAwaitNetworkingMenu(page, networkingTitle) {
  for (let attempt = 0; attempt < 2; ++attempt) {
    await page.waitForTimeout(300);
    await clickCanvasGameCoord(page, POPUP_OK_BUTTON.x, POPUP_OK_BUTTON.y);
    try {
      await waitForRegionToShow(
        page,
        NETWORKING_TITLE_REGION,
        networkingTitle,
        'the networking menu should be redrawn after dismissing the popup',
        10_000,
      );
      return;
    } catch (error) {
      if (attempt === 1) {
        throw error;
      }
    }
  }
}

// Menu transitions re-init buttons and clear input state, so a single click
// dispatched mid-transition can be dropped on the floor. Re-opening the
// networking menu therefore retries: click NETWORKING, give the menu a
// bounded window to appear, and try again if the click was eaten.
async function reopenNetworkingMenu(page, networkingTitle) {
  let lastError;
  for (let attempt = 0; attempt < 3; ++attempt) {
    await page.waitForTimeout(750);
    await openNetworkingFromTeamBuild(page);
    try {
      await waitForRegionToShow(
        page,
        NETWORKING_TITLE_REGION,
        networkingTitle,
        'the networking menu should re-open',
        7_000,
      );
      return;
    } catch (error) {
      lastError = error;
    }
  }
  throw lastError;
}

async function pressWebKey(page, key, settlingMs = 400) {
  await page.keyboard.press(key, { delay: 75 });
  await page.waitForTimeout(settlingMs);
}

// JOIN with a blank room field opens the canvas-native prompt on desktop and
// the mirrored DOM field on touch. This runtime seam waits for either path
// without relying on transient darkened pixels.
async function submitBlankRoomCodeJoin(page, roomCode) {
  await clickCanvasGameCoord(
    page,
    NETWORKING_MENU_BUTTONS.join.x,
    NETWORKING_MENU_BUTTONS.join.y,
  );
  await page.waitForFunction(
    () => window.__opengladTextInputActive === true,
    null,
    { timeout: 20_000 },
  );
  if (roomCode !== null) {
    await page.keyboard.type(roomCode, { delay: 75 });
  }
  await pressWebKey(page, 'Enter');
  await page.waitForFunction(
    () => window.__opengladTextInputActive === false,
    null,
    { timeout: 20_000 },
  );
}

async function loadPickerWithSkippedIntro(page, relayBaseUrlForTests) {
  await page.addInitScript(
    ({ relayBaseUrl }) => {
      window.__opengladSkipIntroForTests = true;
      // The company-era main menu boots into the no-company variant ("NO
      // COMPANY YET - BEGIN A NEW GAME") unless a company save exists, and
      // the CONTINUE half at (114,85) only exists in the with-company
      // variant. Seed the standard single-soldier web company at boot (the
      // same hook the game/jitter/touch specs use) so every navigation in
      // this spec starts from CONTINUE.
      window.__opengladSeedSinglePlayerTeam = true;
      if (relayBaseUrl) {
        window.__opengladRelayBaseUrlForTests = relayBaseUrl;
      }
    },
    { relayBaseUrl: relayBaseUrlForTests || '' },
  );
  await page.goto('/play.html');
  await waitForGameLoad(page);
}

test.describe('Browser networking error paths (unreachable relay)', () => {
  test('HOST failure shows a popup, keeps the menu alive, and never aborts', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    // The shipped default relay is the live deployed worker, so the
    // unreachable-relay failure shape needs an explicit override: the RFC
    // 6761 .invalid TLD is guaranteed to never resolve.
    await loadPickerWithSkippedIntro(page, UNREACHABLE_RELAY_BASE_URL);
    await navigateToNetworkingMenu(page);

    const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);
    const networkingPopupArea = await captureRegion(page, POPUP_REGION);

    // THE BUG: pre-fix, this click aborted the wasm runtime and froze the
    // page. Post-fix the relay failure must surface as a HOST GAME popup.
    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.host.x,
      NETWORKING_MENU_BUTTONS.host.y,
    );
    await waitForRegionToLeave(
      page,
      POPUP_REGION,
      networkingPopupArea,
      'HOST against the unreachable relay should draw an error popup',
    );
    await expectNoWasmAbort(page, errors, 'HOST against unreachable relay');

    // Dismiss the popup via its OK button; the menu must redraw untouched.
    await dismissPopupAndAwaitNetworkingMenu(page, networkingTitle);

    // Idempotence: the replace path shutdown()s the previous lobby client, so
    // a second HOST attempt must fail just as gracefully.
    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.host.x,
      NETWORKING_MENU_BUTTONS.host.y,
    );
    await waitForRegionToLeave(
      page,
      POPUP_REGION,
      networkingPopupArea,
      'a second HOST attempt should surface the error popup again',
    );
    await expectNoWasmAbort(page, errors, 'second HOST attempt');
    await dismissPopupAndAwaitNetworkingMenu(page, networkingTitle);

    // The picker must stay fully interactive: BACK to the team-build menu,
    // then re-open NETWORKING.
    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.back.x,
      NETWORKING_MENU_BUTTONS.back.y,
    );
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'BACK should leave the networking menu',
    );
    await reopenNetworkingMenu(page, networkingTitle);

    await expectNoWasmAbort(page, errors, 'post-recovery navigation');
    assertNoRuntimeErrors(errors, 'HOST unreachable-relay regression');
  });

  test('JOIN with an entered room code never aborts and keeps the picker alive', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await loadPickerWithSkippedIntro(page, UNREACHABLE_RELAY_BASE_URL);
    await continueToTeamBuildMenu(page);
    const statusBaseline = await captureRegion(page, TEAM_BUILD_STATUS_REGION);
    await openNetworkingFromTeamBuild(page);
    const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);

    // The room field starts genuinely blank. JOIN opens a concise room-code
    // prompt; entering a code installs a relay join client whose wss://
    // connection fails asynchronously without aborting the runtime.
    await submitBlankRoomCodeJoin(page, 'GLAD-OFFLINE');
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'JOIN should leave the networking menu for the team-build screen',
    );
    await expectNoWasmAbort(page, errors, 'JOIN against unreachable relay');

    // The join client's live status lines ("Relay: GLAD-OFFLINE" /
    // "Status: connecting") render top-left on the team-build screen.
    await waitForRegionToLeave(
      page,
      TEAM_BUILD_STATUS_REGION,
      statusBaseline,
      'the join client status lines should be drawn on the team-build screen',
    );

    // The picker remains interactive: NETWORKING re-opens over the pending
    // join client.
    await reopenNetworkingMenu(page, networkingTitle);

    await expectNoWasmAbort(page, errors, 'post-JOIN navigation');
    assertNoRuntimeErrors(errors, 'JOIN unreachable-relay regression');
  });
});

test.describe('Browser networking happy path (local relay stub)', () => {
  /** @type {RelayStub} */
  let relayStub;

  test.beforeAll(async () => {
    relayStub = new RelayStub({
      roomCode: 'GLAD-E2E1',
      ownerToken: 'tok-e2e-owner',
    });
    await relayStub.start();
  });

  test.afterAll(async () => {
    if (relayStub) {
      await relayStub.stop();
    }
  });

  test('HOST against a live relay creates a room and shows lobby status', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    // Point the game at the stub through the __opengladRelayBaseUrlForTests
    // hook (read by relay_base_url_or_default on the emscripten path).
    await loadPickerWithSkippedIntro(page, relayStub.baseUrl);
    await continueToTeamBuildMenu(page);
    const statusBaseline = await captureRegion(page, TEAM_BUILD_STATUS_REGION);
    await openNetworkingFromTeamBuild(page);
    const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);

    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.host.x,
      NETWORKING_MENU_BUTTONS.host.y,
    );

    // Protocol-level proof: one room-create POST, then the owner WebSocket
    // connect carrying the room code and owner token we handed back.
    await expect
      .poll(() => relayStub.createRequests.length, {
        message: 'the HOST flow should POST /api/create to the relay stub',
        timeout: 20_000,
      })
      .toBe(1);
    await expect
      .poll(() => relayStub.roomConnections.length, {
        message: 'the HOST flow should open the owner room WebSocket',
        timeout: 20_000,
      })
      .toBe(1);
    const connection = relayStub.roomConnections[0];
    expect(connection.roomCode).toBe('GLAD-E2E1');
    expect(connection.ownerToken).toBe('tok-e2e-owner');

    // A successful HOST leaves the networking menu (a failure would keep it
    // up behind an error popup instead)...
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'a successful HOST should return to the team-build screen',
    );
    // ...and draws the host lobby status lines top-left, whose second line is
    // "Relay: GLAD-E2E1" (the room code from the stub's create response).
    await waitForRegionToLeave(
      page,
      TEAM_BUILD_STATUS_REGION,
      statusBaseline,
      'the host lobby status lines should be drawn on the team-build screen',
    );

    // No error popup: the picker is live, so NETWORKING re-opens on top of
    // the active host lobby. (A modal popup would swallow this click.)
    await reopenNetworkingMenu(page, networkingTitle);

    // The owner relay socket must still be connected.
    expect(connection.closed).toBe(false);

    await expectNoWasmAbort(page, errors, 'HOST against the local relay stub');
    assertNoRuntimeErrors(errors, 'relay stub HOST happy path');
  });
});

test.describe('Browser networking stalled relay create (held route)', () => {
  test('HOST against a relay that never answers times out into a popup', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    // Answer discovery locally, then hold the room-create POST to the shipped
    // default relay host.  This models a relay that accepts the operation but
    // never responds while keeping every request away from the real worker.
    // The page-side EM_ASYNC_JS fetch helper guards this with a
    // feature-detected AbortSignal.timeout(10000), so HOST must fail into
    // its popup within ~10s instead of leaving the picker
    // Asyncify-suspended indefinitely.
    const heldRoutes = [];
    const isDefaultRelay = (url) =>
      url.hostname === DEFAULT_RELAY_HOSTNAME &&
      url.pathname.startsWith(DEFAULT_RELAY_PATH_PREFIX);
    await page.route(isDefaultRelay, async (route) => {
      const request = route.request();
      const url = new URL(request.url());
      if (
        request.method() === 'GET' &&
        url.pathname === `${DEFAULT_RELAY_PATH_PREFIX}/api/rooms`
      ) {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({ rooms: [] }),
        });
        return;
      }
      heldRoutes.push(route);
    });

    try {
      // No relay override: HOST aims at the shipped default relay URL,
      // whose requests the held route above swallows.
      await loadPickerWithSkippedIntro(page);
      await navigateToNetworkingMenu(page);

      const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);
      const networkingPopupArea = await captureRegion(page, POPUP_REGION);

      await clickCanvasGameCoord(
        page,
        NETWORKING_MENU_BUTTONS.host.x,
        NETWORKING_MENU_BUTTONS.host.y,
      );

      // The room-create POST must actually reach the held route (rather
      // than failing fast at the network layer as in the unreachable-relay
      // scenarios above).
      await expect
        .poll(
          () =>
            heldRoutes.filter((route) => {
              const request = route.request();
              return (
                request.method() === 'POST' &&
                new URL(request.url()).pathname ===
                  `${DEFAULT_RELAY_PATH_PREFIX}/api/create`
              );
            }).length,
          {
            message: 'the HOST flow should issue the room-create request',
            timeout: 20_000,
          },
        )
        .toBeGreaterThan(0);

      // AbortSignal.timeout(10000) fires ~10s in; allow 30s for the popup.
      await waitForRegionToLeave(
        page,
        POPUP_REGION,
        networkingPopupArea,
        'HOST against the stalled relay should abort the fetch into an error popup',
        30_000,
      );
      await expectNoWasmAbort(page, errors, 'HOST against a stalled relay');

      await dismissPopupAndAwaitNetworkingMenu(page, networkingTitle);

      // The menu stays interactive: BACK out and re-open NETWORKING.
      await clickCanvasGameCoord(
        page,
        NETWORKING_MENU_BUTTONS.back.x,
        NETWORKING_MENU_BUTTONS.back.y,
      );
      await waitForRegionToLeave(
        page,
        NETWORKING_TITLE_REGION,
        networkingTitle,
        'BACK should leave the networking menu',
      );
      await reopenNetworkingMenu(page, networkingTitle);

      await expectNoWasmAbort(page, errors, 'post-stall navigation');
      assertNoRuntimeErrors(errors, 'stalled relay-create HOST flow');
    } finally {
      for (const route of heldRoutes) {
        await route.abort('timedout').catch(() => {});
      }
      await page.unroute(isDefaultRelay).catch(() => {});
    }
  });
});

test.describe('Browser room discovery remains nonblocking', () => {
  test('BACK responds while the active-room request is still pending', async ({
    page,
  }) => {
    test.setTimeout(120_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors, [/openglad\.pages\.dev/]);

    const heldRoutes = [];
    const isDefaultRoomList = (url) =>
      url.hostname === DEFAULT_RELAY_HOSTNAME &&
      url.pathname === `${DEFAULT_RELAY_PATH_PREFIX}/api/rooms`;
    await page.route(isDefaultRoomList, (route) => {
      heldRoutes.push(route);
    });

    try {
      await loadPickerWithSkippedIntro(page);
      await navigateToNetworkingMenu(page);
      const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);

      await expect
        .poll(() => heldRoutes.length, {
          message: 'the networking menu should start active-room discovery',
          timeout: 20_000,
        })
        .toBeGreaterThan(0);

      // The fetch remains unresolved. BACK must still be sampled, dispatched,
      // and painted promptly; the old Asyncify implementation could not do
      // any of those until its ten-second HTTP timeout elapsed.
      await clickCanvasGameCoord(
        page,
        NETWORKING_MENU_BUTTONS.back.x,
        NETWORKING_MENU_BUTTONS.back.y,
      );
      await waitForRegionToLeave(
        page,
        NETWORKING_TITLE_REGION,
        networkingTitle,
        'BACK should leave the menu while discovery is pending',
        5_000,
      );
      await expectNoWasmAbort(page, errors, 'pending room discovery BACK path');
    } finally {
      for (const route of heldRoutes) {
        await route.abort('timedout').catch(() => {});
      }
      await page.unroute(isDefaultRoomList).catch(() => {});
    }
  });
});

test.describe('Browser networking JOIN failure status (rejected room)', () => {
  /** @type {RelayStub} */
  let relayStub;

  test.beforeEach(async () => {
    relayStub = new RelayStub({
      roomCode: 'GLAD-E2E1',
      ownerToken: 'tok-e2e-owner',
    });
    await relayStub.start();
  });

  test.afterEach(async () => {
    if (relayStub) {
      await relayStub.stop();
    }
  });

  test('JOIN with an unknown room code settles on the connection-failed status', async ({
    page,
  }) => {
    test.setTimeout(240_000);
    const errors = [];
    // The failed WebSocket connects to the local stub are expected chatter.
    attachRuntimeErrorCollectors(page, errors, [
      new RegExp(`127\\.0\\.0\\.1:${relayStub.port}`),
    ]);

    await loadPickerWithSkippedIntro(page, relayStub.baseUrl);
    await continueToTeamBuildMenu(page);
    const statusBaseline = await captureRegion(page, TEAM_BUILD_STATUS_REGION);
    await openNetworkingFromTeamBuild(page);
    const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);

    // PHASE 1 — reference capture: against a relay that never answers the
    // room upgrade, the join client sits in "Status: connecting". This pins
    // the exact pixels of the connecting state so phase 2 can prove the
    // status text really CHANGED to the failure state; a plain
    // left-the-empty-baseline check could not tell the two apart.
    relayStub.unknownRoomBehavior = 'hold';
    await submitBlankRoomCodeJoin(page, 'GLAD-UNKNOWN');
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'JOIN should leave the networking menu for the team-build screen',
    );
    await expect
      .poll(() => relayStub.heldUpgrades.length, {
        message: 'the joiner should reach the stub with the entered room code',
        timeout: 20_000,
      })
      .toBe(1);
    expect(relayStub.heldUpgrades[0].roomCode).toBe('GLAD-UNKNOWN');
    await waitForRegionToLeave(
      page,
      TEAM_BUILD_STATUS_REGION,
      statusBaseline,
      'the join status lines should be drawn while the socket is connecting',
    );
    const connectingStatus = await captureSettledRegion(
      page,
      TEAM_BUILD_STATUS_REGION,
      'the "Status: connecting" join status lines',
    );
    expect(connectingStatus.equals(statusBaseline)).toBe(false);

    // PHASE 2 — the real relay shape: unknown rooms are rejected outright
    // (HTTP 404 on the upgrade), so the socket dies before EVER connecting.
    // Status-line contract: the second line must become
    // "Status: connection failed" — pixels that differ from both the empty
    // baseline and the pinned connecting state, and that stay there.
    relayStub.unknownRoomBehavior = 'reject';
    await reopenNetworkingMenu(page, networkingTitle);
    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.join.x,
      NETWORKING_MENU_BUTTONS.join.y,
    );
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'the second JOIN should leave the networking menu again',
    );
    await expect
      .poll(() => relayStub.rejectedUpgrades.length, {
        message: 'the stub should reject the unknown room upgrade',
        timeout: 20_000,
      })
      .toBe(1);
    expect(relayStub.rejectedUpgrades[0].roomCode).toBe('GLAD-UNKNOWN');

    await waitForRegionToSettleAway(
      page,
      TEAM_BUILD_STATUS_REGION,
      [statusBaseline, connectingStatus],
      'the join status should settle on "Status: connection failed" after the rejected upgrade',
      30_000,
    );
    await expectNoWasmAbort(page, errors, 'JOIN against the rejecting relay stub');

    // The picker stays interactive over the failed join client.
    await reopenNetworkingMenu(page, networkingTitle);
    await expectNoWasmAbort(page, errors, 'post-failure navigation');
    assertNoRuntimeErrors(errors, 'JOIN rejected-room failure status');
  });
});

test.describe('Browser networking two-player lobby (relay stub)', () => {
  /** @type {RelayStub} */
  let relayStub;

  test.beforeEach(async () => {
    relayStub = new RelayStub({
      roomCode: 'GLAD-DUO1',
      ownerToken: 'tok-e2e-owner-duo',
    });
    await relayStub.start();
  });

  test.afterEach(async () => {
    if (relayStub) {
      await relayStub.stop();
    }
  });

  test('HOST and JOIN from two browser contexts meet in one relay lobby', async ({
    browser,
  }) => {
    test.setTimeout(300_000);
    const stubUrlPattern = new RegExp(`127\\.0\\.0\\.1:${relayStub.port}`);
    const hostContext = await browser.newContext();
    const joinContext = await browser.newContext();
    try {
      // Page A hosts the room through the stub.
      const hostPage = await hostContext.newPage();
      const hostErrors = [];
      attachRuntimeErrorCollectors(hostPage, hostErrors, [stubUrlPattern]);
      await loadPickerWithSkippedIntro(hostPage, relayStub.baseUrl);
      await continueToTeamBuildMenu(hostPage);
      const hostStatusBaseline = await captureRegion(
        hostPage,
        TEAM_BUILD_STATUS_REGION,
      );
      await openNetworkingFromTeamBuild(hostPage);
      const hostNetworkingTitle = await captureRegion(
        hostPage,
        NETWORKING_TITLE_REGION,
      );

      await clickCanvasGameCoord(
        hostPage,
        NETWORKING_MENU_BUTTONS.host.x,
        NETWORKING_MENU_BUTTONS.host.y,
      );

      await expect
        .poll(() => relayStub.createRequests.length, {
          message: 'the HOST flow should POST /api/create to the relay stub',
          timeout: 20_000,
        })
        .toBe(1);
      await expect
        .poll(() => relayStub.roomConnections.length, {
          message: 'the HOST flow should open the owner room WebSocket',
          timeout: 20_000,
        })
        .toBe(1);
      const ownerConnection = relayStub.roomConnections[0];
      expect(ownerConnection.roomCode).toBe('GLAD-DUO1');
      expect(ownerConnection.ownerToken).toBe('tok-e2e-owner-duo');
      expect(ownerConnection.peerId).toBe(1);

      await waitForRegionToLeave(
        hostPage,
        NETWORKING_TITLE_REGION,
        hostNetworkingTitle,
        'a successful HOST should return to the team-build screen',
      );
      await waitForRegionToLeave(
        hostPage,
        TEAM_BUILD_STATUS_REGION,
        hostStatusBaseline,
        'the host lobby status lines should be drawn on the team-build screen',
      );
      await expectNoWasmAbort(hostPage, hostErrors, 'hosting the two-player lobby');

      // Page B, in an isolated browser context, taps the room surfaced by
      // ACTIVE GAMES. This proves discovery and row dispatch end to end; no
      // room-code typing is involved.
      const joinPage = await joinContext.newPage();
      const joinErrors = [];
      attachRuntimeErrorCollectors(joinPage, joinErrors, [stubUrlPattern]);
      const listRequestsBeforeJoin = relayStub.listRequests.length;
      await loadPickerWithSkippedIntro(joinPage, relayStub.baseUrl);
      await continueToTeamBuildMenu(joinPage);
      const joinStatusBaseline = await captureRegion(
        joinPage,
        TEAM_BUILD_STATUS_REGION,
      );
      await openNetworkingFromTeamBuild(joinPage);
      const joinNetworkingTitle = await captureRegion(
        joinPage,
        NETWORKING_TITLE_REGION,
      );

      await expect
        .poll(() => relayStub.listRequests.length, {
          message: 'the join page should discover the active relay room',
          timeout: 20_000,
        })
        .toBeGreaterThan(listRequestsBeforeJoin);
      // Request receipt precedes Promise resolution, C++ polling, the
      // one-frame safety deferral, and drawing. Wait for the committed row
      // diagnostic instead of racing those stages with a fixed sleep.
      await joinPage.waitForFunction(
        (roomCode) =>
          window.__opengladRelayRoomSnapshot?.firstRoomCode === roomCode,
        'GLAD-DUO1',
        { timeout: 20_000 },
      );
      await clickCanvasGameCoord(
        joinPage,
        ACTIVE_ROOM_FIRST.x,
        ACTIVE_ROOM_FIRST.y,
      );
      await waitForRegionToLeave(
        joinPage,
        NETWORKING_TITLE_REGION,
        joinNetworkingTitle,
        'JOIN should leave the networking menu for the team-build screen',
      );

      await expect
        .poll(() => relayStub.roomConnections.length, {
          message: 'the joiner should open the second room WebSocket',
          timeout: 20_000,
        })
        .toBe(2);
      const joinerConnection = relayStub.roomConnections[1];
      expect(joinerConnection.roomCode).toBe('GLAD-DUO1');
      expect(joinerConnection.ownerToken).toBe(null);
      expect(joinerConnection.peerId).toBe(2);

      // The relay notified the owner about the joiner (the stub writes the
      // peer_joined control frame synchronously during the joiner's upgrade).
      expect(
        ownerConnection.sentControls.filter(
          (message) => message.type === 'peer_joined' && message.peer_id === 2,
        ),
      ).toHaveLength(1);

      // Protocol-level proof that the joiner reached the host's LobbyServer
      // through the relay: data frames forwarded joiner -> host (the lobby
      // join message)...
      await expect
        .poll(() => relayStub.forwardedFrameCount(2, 1), {
          message: 'the joiner lobby traffic should be forwarded to the host',
          timeout: 30_000,
        })
        .toBeGreaterThan(0);
      // ...and host -> joiner (the authoritative lobby state answer).
      await expect
        .poll(() => relayStub.forwardedFrameCount(1, 2), {
          message: 'the host lobby state should be forwarded back to the joiner',
          timeout: 30_000,
        })
        .toBeGreaterThan(0);

      // The joiner draws its live status lines ("Relay: GLAD-DUO1" /
      // "Status: connected") and they stabilize away from the empty baseline.
      await waitForRegionToLeave(
        joinPage,
        TEAM_BUILD_STATUS_REGION,
        joinStatusBaseline,
        'the joiner status lines should be drawn on the team-build screen',
      );
      const joinerStatus = await captureSettledRegion(
        joinPage,
        TEAM_BUILD_STATUS_REGION,
        'the joiner lobby status lines',
      );
      expect(joinerStatus.equals(joinStatusBaseline)).toBe(false);

      // The host's visible lines stay "Direct: ..." / "Relay: GLAD-DUO1":
      // its "Lobby: 2 players" line is the THIRD status line and the
      // team-build screen renders only the first two, so the stub-side
      // forwarded-frame counts above are the authoritative two-player
      // signal rather than host pixels.
      expect(ownerConnection.closed).toBe(false);
      expect(joinerConnection.closed).toBe(false);

      // --- §4.8 READY-before-GO over the relay (§2.6 state table). ---

      // 1. Denied GO: the host's GO is gated on the joiner's READY (§2.6
      // state 3). The denial is synchronous and client-side on the browser
      // host: a "WAITING FOR:" popup, nothing sent to the server, and the
      // base camp stays alive — no hang, no abort.
      const hostPopupBaseline = await captureSettledRegion(
        hostPage,
        POPUP_REGION,
        'the host base-camp popup area before the gated GO',
      );
      let denialPopupUp = false;
      for (let attempt = 0; attempt < 3 && !denialPopupUp; ++attempt) {
        await clickCanvasGameCoord(
          hostPage,
          GO_READY_BUTTON.x,
          GO_READY_BUTTON.y,
        );
        try {
          await waitForRegionToLeave(
            hostPage,
            POPUP_REGION,
            hostPopupBaseline,
            'GO before the joiner is ready should draw the WAITING FOR popup',
            7_000,
          );
          denialPopupUp = true;
        } catch (error) {
          if (attempt === 2) {
            throw error;
          }
        }
      }
      await expectNoWasmAbort(
        hostPage,
        hostErrors,
        'host GO denied while the joiner is unready',
      );
      // The denied GO must not have left the picker on either page.
      expect(
        await hostPage.evaluate(() => window.__opengladGameState === 2),
      ).toBe(false);
      expect(
        await joinPage.evaluate(() => window.__opengladGameState === 2),
      ).toBe(false);
      // Dismiss the popup; the base camp must redraw exactly as captured.
      for (let attempt = 0; attempt < 2; ++attempt) {
        await hostPage.waitForTimeout(300);
        await clickCanvasGameCoord(
          hostPage,
          POPUP_OK_BUTTON.x,
          POPUP_OK_BUTTON.y,
        );
        try {
          await waitForRegionToShow(
            hostPage,
            POPUP_REGION,
            hostPopupBaseline,
            'the base camp should be redrawn after dismissing WAITING FOR',
            10_000,
          );
          break;
        } catch (error) {
          if (attempt === 1) {
            throw error;
          }
        }
      }

      // 2. The joiner readies via the GO-slot READY twin. Two independent
      // confirmations: the joiner's own slot flips READY -> UNREADY
      // locally, and the HOST's GO face leaves its gated state (the ready
      // travelled joiner -> relay -> host lobby server -> host §2.6
      // presentation).
      const hostGoGated = await captureSettledRegion(
        hostPage,
        GO_READY_SLOT_REGION,
        'the host GO slot while gated on the unready joiner',
      );
      const joinReadyBaseline = await captureSettledRegion(
        joinPage,
        GO_READY_SLOT_REGION,
        'the joiner READY slot before the toggle',
      );
      let readyToggled = false;
      for (let attempt = 0; attempt < 3 && !readyToggled; ++attempt) {
        await clickCanvasGameCoord(
          joinPage,
          GO_READY_BUTTON.x,
          GO_READY_BUTTON.y,
        );
        try {
          await waitForRegionToLeave(
            joinPage,
            GO_READY_SLOT_REGION,
            joinReadyBaseline,
            'the joiner READY toggle should flip to UNREADY',
            7_000,
          );
          readyToggled = true;
        } catch (error) {
          if (attempt === 2) {
            throw error;
          }
        }
      }
      await waitForRegionToLeave(
        hostPage,
        GO_READY_SLOT_REGION,
        hostGoGated,
        "the joiner's READY should reach the host and ungate its GO face",
        30_000,
      );

      // 3. With the joiner ready, host GO starts the game on BOTH machines
      // through the relay (the owner-locked default install). The picker
      // runs under Asyncify, so gameplay liveness = __opengladGameState 2
      // or the first render sample.
      const inGameplay = () =>
        window.__opengladGameState === 2 ||
        Boolean(window.__opengladLatestRenderSample);
      let hostStarted = false;
      for (let attempt = 0; attempt < 3 && !hostStarted; ++attempt) {
        await clickCanvasGameCoord(
          hostPage,
          GO_READY_BUTTON.x,
          GO_READY_BUTTON.y,
        );
        try {
          await hostPage.waitForFunction(inGameplay, null, {
            timeout: 45_000,
          });
          hostStarted = true;
        } catch (error) {
          if (attempt === 2) {
            throw error;
          }
        }
      }
      await joinPage.waitForFunction(inGameplay, null, { timeout: 90_000 });
      await expectNoWasmAbort(
        hostPage,
        hostErrors,
        'host gameplay start over the relay',
      );
      await expectNoWasmAbort(
        joinPage,
        joinErrors,
        'joiner gameplay start over the relay',
      );

      await expectNoWasmAbort(
        hostPage,
        hostErrors,
        'host page after the joiner arrived',
      );
      await expectNoWasmAbort(
        joinPage,
        joinErrors,
        'join page after entering the lobby',
      );
      assertNoRuntimeErrors(hostErrors, 'two-player lobby (host page)');
      assertNoRuntimeErrors(joinErrors, 'two-player lobby (join page)');
    } finally {
      await joinContext.close();
      await hostContext.close();
    }
  });
});

test.describe('Browser networking host relay drop (local relay stub)', () => {
  /** @type {RelayStub} */
  let relayStub;

  test.beforeEach(async () => {
    relayStub = new RelayStub({
      roomCode: 'GLAD-E2E2',
      ownerToken: 'tok-e2e-owner-drop',
    });
    await relayStub.start();
  });

  test.afterEach(async () => {
    if (relayStub) {
      await relayStub.stop();
    }
  });

  test('host relay socket drop settles on the connection-lost relay line', async ({
    page,
  }) => {
    test.setTimeout(240_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors, [
      new RegExp(`127\\.0\\.0\\.1:${relayStub.port}`),
    ]);

    await loadPickerWithSkippedIntro(page, relayStub.baseUrl);
    await continueToTeamBuildMenu(page);
    const statusBaseline = await captureRegion(page, TEAM_BUILD_STATUS_REGION);
    await openNetworkingFromTeamBuild(page);
    const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);

    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.host.x,
      NETWORKING_MENU_BUTTONS.host.y,
    );
    await expect
      .poll(() => relayStub.roomConnections.length, {
        message: 'the HOST flow should open the owner room WebSocket',
        timeout: 20_000,
      })
      .toBe(1);
    const ownerConnection = relayStub.roomConnections[0];
    expect(ownerConnection.roomCode).toBe('GLAD-E2E2');
    expect(ownerConnection.ownerToken).toBe('tok-e2e-owner-drop');

    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'a successful HOST should return to the team-build screen',
    );
    await waitForRegionToLeave(
      page,
      TEAM_BUILD_STATUS_REGION,
      statusBaseline,
      'the host lobby status lines should be drawn on the team-build screen',
    );
    // Pin the healthy hosted pixels ("Direct: ..." / "Relay: GLAD-E2E2") so
    // the drop below has a stable reference to change away from.
    const hostedStatus = await captureSettledRegion(
      page,
      TEAM_BUILD_STATUS_REGION,
      'the healthy host lobby status lines',
    );
    expect(hostedStatus.equals(statusBaseline)).toBe(false);
    await expectNoWasmAbort(page, errors, 'hosting before the relay drop');

    // Server-side drop: the relay closes the owner socket after the room was
    // created successfully.
    ownerConnection.close();
    await expect
      .poll(() => ownerConnection.closed, {
        message: 'the stub should observe the owner socket closing',
        timeout: 10_000,
      })
      .toBe(true);

    // Status-line contract: the "Relay: GLAD-E2E2" line becomes
    // "Relay: connection lost" — pixels that differ from both the empty
    // baseline and the healthy hosted state, and that stay there.
    await waitForRegionToSettleAway(
      page,
      TEAM_BUILD_STATUS_REGION,
      [statusBaseline, hostedStatus],
      'the host status should settle on the connection-lost relay line',
      30_000,
    );
    await expectNoWasmAbort(page, errors, 'host after the relay drop');

    // The picker stays interactive over the degraded host lobby.
    await reopenNetworkingMenu(page, networkingTitle);
    await expectNoWasmAbort(page, errors, 'post-drop navigation');
    assertNoRuntimeErrors(errors, 'host relay drop status');
  });
});

// On web, the Keyboard Lock machinery was removed: the engine swallows
// physical Escape entirely (browser-reserved for fullscreen exit) and
// Backspace is the universal back/cancel key — EXCEPT while a text prompt is
// active, where Backspace keeps deleting characters. The networking menu is
// the ideal probe: its BACK button carries the back-key hotkey, and its ROOM
// VALUE field opens a prompt_for_string text prompt.
test.describe('Browser web back key (Backspace replaces Escape)', () => {
  // The relay-only web ROOM CODE field (x 110..270, y 40..55).
  const ROOM_VALUE_FIELD = { x: 190, y: 47 };
  const ROOM_VALUE_FIELD_REGION = { x: 108, y: 36, w: 164, h: 23 };
  // prompt_for_string draws its frame from (53,40) to (242,80) over a
  // darkened backdrop: the message line at y=50 and the input row at (58,60).
  const ROOM_PROMPT_REGION = { x: 48, y: 36, w: 200, h: 48 };

  // Open the networking menu and PROVE it opened: the title region must
  // change away from its team-build content and settle on the "NETWORKING"
  // headline. A silently-eaten click would otherwise let the back-key
  // assertions pass vacuously against the wrong screen.
  async function openNetworkingMenuWithProof(page) {
    await continueToTeamBuildMenu(page);
    const preOpenTitle = await captureRegion(page, NETWORKING_TITLE_REGION);
    await openNetworkingFromTeamBuild(page);
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      preOpenTitle,
      'the networking menu should open (title region should change)',
    );
    return await captureSettledRegion(
      page,
      NETWORKING_TITLE_REGION,
      'the networking menu headline',
    );
  }

  test('Backspace backs out of the networking menu; Escape is inert', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    // No relay traffic happens in this flow; the unreachable override keeps
    // any incidental probe away from the live deployed relay.
    await loadPickerWithSkippedIntro(page, UNREACHABLE_RELAY_BASE_URL);
    const networkingTitle = await openNetworkingMenuWithProof(page);

    // Emscripten starts forwarding keyboard input after a real canvas click.
    // (10,10) sits outside the networking panel frame (18,14)-(302,184), so
    // the click is inert.
    await clickCanvasGameCoord(page, 10, 10);

    // HEADLINE: physical Escape is swallowed by the web back-key remap. The
    // menu must still be up, byte-identical, a second later.
    await pressWebKey(page, 'Escape', 1_000);
    expect(
      (await captureRegion(page, NETWORKING_TITLE_REGION)).equals(
        networkingTitle,
      ),
      'Escape must not leave the networking menu',
    ).toBe(true);

    // Backspace is the back key: it fires the BACK hotkey and returns to the
    // team-build screen.
    await pressWebKey(page, 'Backspace');
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'Backspace should leave the networking menu for the team-build screen',
    );

    // The picker stays interactive: the same networking menu re-opens, which
    // also validates that the pinned headline pixels really were the
    // networking menu.
    await reopenNetworkingMenu(page, networkingTitle);

    await expectNoWasmAbort(page, errors, 'Backspace menu navigation');
    assertNoRuntimeErrors(errors, 'Backspace/Escape menu navigation');
  });

  test('text prompt: Backspace deletes, Escape does not cancel, Enter accepts', async ({
    page,
  }) => {
    test.setTimeout(180_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    await loadPickerWithSkippedIntro(page, UNREACHABLE_RELAY_BASE_URL);
    const networkingTitle = await openNetworkingMenuWithProof(page);

    // Pin the genuinely blank room-code field so the accepted edit below has
    // a reference to change away from.
    const roomFieldBefore = await captureSettledRegion(
      page,
      ROOM_VALUE_FIELD_REGION,
      'the ROOM VALUE field before editing',
    );
    const promptBaseline = await captureRegion(page, ROOM_PROMPT_REGION);

    // Click the ROOM VALUE field: prompt_for_string darkens the screen and
    // opens the JOIN ROOM CODE text prompt.
    await clickCanvasGameCoord(page, ROOM_VALUE_FIELD.x, ROOM_VALUE_FIELD.y);
    await waitForRegionToLeave(
      page,
      ROOM_PROMPT_REGION,
      promptBaseline,
      'clicking the ROOM VALUE field should open the text prompt',
    );
    const promptOpened = await captureSettledRegion(
      page,
      ROOM_PROMPT_REGION,
      'the freshly opened room-code prompt',
    );

    // Type a value into the blank prompt; it redraws after every key event.
    await page.keyboard.type('glad', { delay: 120 });
    await waitForRegionToLeave(
      page,
      ROOM_PROMPT_REGION,
      promptOpened,
      'typing should redraw the prompt with the typed text',
    );
    const promptTyped = await captureSettledRegion(
      page,
      ROOM_PROMPT_REGION,
      'the prompt after typing',
    );

    // HEADLINE: during text input Backspace must keep DELETING (one char),
    // not act as the back key.
    await pressWebKey(page, 'Backspace');
    await waitForRegionToLeave(
      page,
      ROOM_PROMPT_REGION,
      promptTyped,
      'Backspace should delete one character inside the prompt',
    );
    const promptAfterBackspace = await captureSettledRegion(
      page,
      ROOM_PROMPT_REGION,
      'the prompt after one Backspace',
    );
    expect(promptAfterBackspace.equals(promptOpened)).toBe(false);
    // Still inside the prompt: the networking headline is darkened away.
    expect(
      (await captureRegion(page, NETWORKING_TITLE_REGION)).equals(
        networkingTitle,
      ),
    ).toBe(false);

    // Escape must NOT cancel the prompt (it is swallowed outright): the
    // prompt stays open and byte-identical.
    await pressWebKey(page, 'Escape', 1_000);
    expect(
      (await captureRegion(page, ROOM_PROMPT_REGION)).equals(
        promptAfterBackspace,
      ),
      'Escape must neither cancel nor change the text prompt',
    ).toBe(true);

    // Enter accepts: back to the networking menu with the edited value
    // ("gla") in the ROOM VALUE field.
    await pressWebKey(page, 'Enter');
    await waitForRegionToShow(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'Enter should accept the prompt back into the networking menu',
    );
    const roomFieldAfter = await captureSettledRegion(
      page,
      ROOM_VALUE_FIELD_REGION,
      'the ROOM VALUE field after the accepted edit',
    );
    expect(roomFieldAfter.equals(roomFieldBefore)).toBe(false);

    await expectNoWasmAbort(page, errors, 'text prompt back-key handling');
    assertNoRuntimeErrors(errors, 'text prompt Backspace/Escape/Enter flow');
  });
});

// Opt-in end-to-end proof against the LIVE deployed relay
// (https://openglad-relay.yans.workers.dev). Off by default: CI must stay
// hermetic and must not create real rooms on the production worker.
// Run with: OPENGLAD_E2E_LIVE_RELAY=1 npx playwright test wasm-networking
test.describe('Browser networking live deployed relay (opt-in)', () => {
  test.skip(
    process.env.OPENGLAD_E2E_LIVE_RELAY !== '1',
    'live-relay test is opt-in: set OPENGLAD_E2E_LIVE_RELAY=1',
  );

  async function fetchLiveRooms() {
    // Node's global fetch, from the test process — deliberately NOT the
    // browser page, so the listing is independent evidence.
    const response = await fetch(`${LIVE_RELAY_BASE_URL}/api/rooms`);
    if (!response.ok) {
      throw new Error(`live relay /api/rooms failed: HTTP ${response.status}`);
    }
    const rooms = await response.json();
    if (!Array.isArray(rooms)) {
      throw new Error('live relay /api/rooms did not return an array');
    }
    return rooms;
  }

  test('HOST with no override creates a real room on the deployed worker', async ({
    page,
  }) => {
    test.setTimeout(300_000);
    const errors = [];
    attachRuntimeErrorCollectors(page, errors);

    // Other rooms may legitimately exist on the live worker; identify OUR
    // room by the code minted for this test run.
    const baselineCodes = new Set(
      (await fetchLiveRooms()).map((room) => room.code),
    );

    // Observe the page's real create POST (passively — no routing, so the
    // request truly reaches the deployed worker).
    const createExchanges = [];
    page.on('response', (response) => {
      let url;
      try {
        url = new URL(response.url());
      } catch (error) {
        return;
      }
      if (
        url.hostname === DEFAULT_RELAY_HOSTNAME &&
        url.pathname === `${DEFAULT_RELAY_PATH_PREFIX}/api/create` &&
        response.request().method() === 'POST'
      ) {
        createExchanges.push({
          response,
          hostName: url.searchParams.get('host') || '',
        });
      }
    });

    // NO relay override: this exercises the shipped default URL end to end.
    await loadPickerWithSkippedIntro(page);
    await navigateToNetworkingMenu(page);
    const networkingTitle = await captureRegion(page, NETWORKING_TITLE_REGION);

    await clickCanvasGameCoord(
      page,
      NETWORKING_MENU_BUTTONS.host.x,
      NETWORKING_MENU_BUTTONS.host.y,
    );

    await expect
      .poll(() => createExchanges.length, {
        message: 'the HOST flow should POST /api/create to the live relay',
        timeout: 60_000,
      })
      .toBeGreaterThan(0);
    const createExchange = createExchanges[0];
    expect(createExchange.response.ok()).toBe(true);
    const created = await createExchange.response.json();
    expect(typeof created.room_code).toBe('string');
    expect(created.room_code.length).toBeGreaterThan(0);
    expect(baselineCodes.has(created.room_code)).toBe(false);

    // A successful HOST leaves the networking menu for the team-build screen
    // (a failure would keep it up behind an error popup instead).
    await waitForRegionToLeave(
      page,
      NETWORKING_TITLE_REGION,
      networkingTitle,
      'a successful HOST should return to the team-build screen',
      60_000,
    );
    await expectNoWasmAbort(page, errors, 'HOST against the live relay');

    // The worker lists the room publicly once the owner socket is connected.
    // Find our room by code and check the host name it was created with
    // (the worker trims and caps it at 64 chars).
    let listedRoom = null;
    await expect
      .poll(
        async () => {
          const rooms = await fetchLiveRooms();
          listedRoom =
            rooms.find((room) => room.code === created.room_code) || null;
          return Boolean(listedRoom);
        },
        {
          message: 'the live room list should include the freshly hosted room',
          timeout: 120_000,
        },
      )
      .toBe(true);
    expect(listedRoom.host_name).toBe(
      createExchange.hostName.trim().slice(0, 64),
    );
    expect(listedRoom.player_count).toBeGreaterThan(0);

    assertNoRuntimeErrors(errors, 'live relay HOST');

    // Cleanup: closing the page closes the owner relay socket; the worker
    // immediately unlists the now-empty room (player_count 0 entries are
    // hidden, and the empty-room alarm deletes it later).
    await page.close();
    await expect
      .poll(
        async () => {
          const rooms = await fetchLiveRooms();
          return rooms.some((room) => room.code === created.room_code);
        },
        {
          message: 'the hosted room should leave the live listing after close',
          timeout: 120_000,
        },
      )
      .toBe(false);
  });
});
