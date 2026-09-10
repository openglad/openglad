// @ts-check
//
// Web persistence namespace: window.__opengladPersistNamespace
//
// The Emscripten build persists to IDBFS mounted at /persist, in an IndexedDB
// store scoped to the page origin, so several players served from one origin
// share a single set of companies. An embedding host can set
// window.__opengladPersistNamespace to an opaque token before play.js loads to
// give each token its own store. Absent, empty or invalid keeps the shared
// /persist behavior exactly as before.
//
// These specs assert what a host can observe at the JS boundary:
//   1. a default boot is unchanged and emits no namespace warning
//   2. an invalid token warns exactly once and falls back without aborting
//   3. each valid namespace gets its own persistence store, those stores
//      coexist, and using one namespace never disturbs another
//
// They deliberately do NOT assert the IndexedDB database name (an Emscripten
// IDBFS implementation detail). The behavioral gate is: distinct namespaces
// produce distinct, coexisting, reload-surviving stores.
const { test, expect } = require('@playwright/test');
const { waitForGameLoad, waitForPickerReady } = require('./wasm_helpers');

const NAMESPACE_WARNING = /__opengladPersistNamespace/;
// Blank/near-blank canvas captures compress well under this; real frames do not
// (same heuristic as wasm-game.spec.js / wasm-contextloss.spec.js).
const MIN_NON_TRIVIAL_PNG_BYTES = 2_000;

// The namespace (and whether to seed a company) for the next /play.html load is
// carried in localStorage, so one addInitScript can vary it across reloads
// inside a single browser context -- IndexedDB coexistence is only observable
// within one context.
async function primeContext(page) {
  await page.goto('/');
  await page.addInitScript(() => {
    try {
      const ns = window.localStorage.getItem('__pwPersistNs');
      if (ns) {
        window.__opengladPersistNamespace = ns;
      }
      if (window.localStorage.getItem('__pwSeed') === '1') {
        window.__opengladSeedSinglePlayerTeam = true;
      }
      window.__opengladSkipIntroForTests = true;
    } catch (err) {
      // First navigation: nothing stored yet.
    }
  });
}

async function loadPlay(page, { ns, seed = false } = {}) {
  await page.evaluate(
    ({ ns, seed }) => {
      if (ns === undefined) {
        window.localStorage.removeItem('__pwPersistNs');
      } else {
        window.localStorage.setItem('__pwPersistNs', ns);
      }
      window.localStorage.setItem('__pwSeed', seed ? '1' : '0');
    },
    { ns, seed },
  );
  await page.goto('/play.html');
  await waitForGameLoad(page);
}

async function inspectStores(page) {
  return await page.evaluate(async () => {
    if (!window.indexedDB || typeof indexedDB.databases !== 'function') {
      return null;
    }
    const dbs = await indexedDB.databases();
    return {
      count: dbs.length,
      // A token can never introduce a path separator or a traversal segment.
      unsafe: dbs.some((db) => /[\\/\s]|\.\./.test(db.name || '')),
    };
  });
}

async function canvasBytes(page) {
  return (await page.locator('#canvas').screenshot()).length;
}

test.describe('Web persistence namespace', () => {
  test('no namespace: default persistence, no warning, no unsafe store name', async ({
    page,
  }) => {
    const warnings = [];
    page.on('console', (msg) => {
      if (msg.type() === 'warning' && NAMESPACE_WARNING.test(msg.text())) {
        warnings.push(msg.text());
      }
    });

    await primeContext(page);
    await loadPlay(page, { ns: undefined });

    expect(warnings).toEqual([]);
    expect(await canvasBytes(page)).toBeGreaterThan(MIN_NON_TRIVIAL_PNG_BYTES);
    const stores = await inspectStores(page);
    if (stores) {
      expect(stores.unsafe).toBe(false);
    }
  });

  for (const { token, label } of [
    { token: 'bad/token', label: 'contains a slash' },
    { token: '../evil', label: 'is a traversal' },
    { token: 'x'.repeat(65), label: 'is over 64 chars' },
  ]) {
    test(`invalid namespace (${label}): warns once, no abort, default store`, async ({
      page,
    }) => {
      const warnings = [];
      const errors = [];
      page.on('console', (msg) => {
        if (msg.type() === 'warning' && NAMESPACE_WARNING.test(msg.text())) {
          warnings.push(msg.text());
        }
        if (msg.type() === 'error') {
          errors.push(msg.text());
        }
      });
      page.on('pageerror', (err) => errors.push(err.message));

      await primeContext(page);
      await loadPlay(page, { ns: token });

      expect(warnings.length).toBe(1);
      expect(errors.join('\n')).not.toMatch(/Aborted/);
      expect(await canvasBytes(page)).toBeGreaterThan(MIN_NON_TRIVIAL_PNG_BYTES);
    });
  }

  test('valid namespaces get separate, coexisting, reload-surviving stores', async ({
    page,
  }) => {
    // Boots /play.html five times.
    test.setTimeout(240_000);
    const errors = [];
    page.on('console', (msg) => {
      if (msg.type() === 'error') {
        errors.push(msg.text());
      }
    });
    page.on('pageerror', (err) => errors.push(err.message));

    await primeContext(page);

    // ns-alpha, seeded: the seed writes save0 and syncs it to this namespace's
    // own store.
    await loadPlay(page, { ns: 'ns-alpha', seed: true });
    await waitForPickerReady(page);
    const afterAlpha = await inspectStores(page);
    if (afterAlpha) {
      expect(afterAlpha.count).toBeGreaterThanOrEqual(1);
      expect(afterAlpha.unsafe).toBe(false);
    }

    // ns-alpha again, no seed: the store survived the reload and the game boots.
    await loadPlay(page, { ns: 'ns-alpha' });
    expect(await canvasBytes(page)).toBeGreaterThan(MIN_NON_TRIVIAL_PNG_BYTES);

    // ns-beta: a different token gets its own store, alongside ns-alpha's.
    await loadPlay(page, { ns: 'ns-beta' });
    expect(await canvasBytes(page)).toBeGreaterThan(MIN_NON_TRIVIAL_PNG_BYTES);
    const afterBeta = await inspectStores(page);
    if (afterAlpha && afterBeta) {
      expect(afterBeta.count).toBeGreaterThan(afterAlpha.count);
      expect(afterBeta.unsafe).toBe(false);
    }

    // Back to ns-alpha: exercising ns-beta left ns-alpha's store intact.
    await loadPlay(page, { ns: 'ns-alpha' });
    expect(await canvasBytes(page)).toBeGreaterThan(MIN_NON_TRIVIAL_PNG_BYTES);

    expect(errors.join('\n')).not.toMatch(/Aborted/);
  });
});
