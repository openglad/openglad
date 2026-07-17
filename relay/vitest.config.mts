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
        },
      },
    }),
  ],
  test: {
    testTimeout: 10_000,
  },
});
