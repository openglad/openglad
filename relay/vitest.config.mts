import { cloudflareTest } from "@cloudflare/vitest-pool-workers";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [
    cloudflareTest({
      main: "./src/index.ts",
      wrangler: {
        configPath: "./wrangler.toml",
      },
      miniflare: {
        bindings: {
          // Small per-connection message budget so the flood test trips the
          // limit within one budget window even on a heavily loaded machine.
          // Must stay above what any non-flood test sends per second (~20)
          // and match TEST_MESSAGE_BUDGET in test/relay.test.ts.
          MESSAGE_BUDGET_MAX_MESSAGES: "50",
          // Short cloud-save TTL so the save-vault expiry tests can age
          // last_access deterministically (they rewind the stored timestamp
          // and drive the alarm directly; no wall-clock waiting). Must match
          // TEST_CLOUD_SAVE_TTL_MS in test/save-vault.test.ts.
          CLOUD_SAVE_TTL_MS: "60000",
        },
      },
    }),
  ],
  test: {
    testTimeout: 10_000,
  },
});
