/// <reference path="../node_modules/@cloudflare/vitest-pool-workers/types/cloudflare-test.d.ts" />

import type { Env as AppEnv } from "../src/types";

declare global {
  namespace Cloudflare {
    interface Env extends AppEnv {}
  }
}

export {};
