# Execution Order

**Each phase is completed and fully verified before the next begins. There is no parallelism — phases are executed strictly one at a time, in order.** Phase 31 (relay) is the sole exception: its TypeScript relay server code can be developed in parallel with Phases 23-30 since it's a standalone project with no C++ build dependencies — only the client-side `RelayWebSocketTransport` integration requires Phases 25/26 to be complete.

All phases are executed sequentially, one at a time, in order: Phase 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31.
