# Phase 4: InputState Serialization

> **See also:** [Context & Key Decisions](../common/context.md) | [Verification Strategy](../common/verification-strategy.md)

`InputState` is what clients send to the server every tick. Defined at `include/openglad/gameplay/input_state.h`.

Structure: `PlayerInput` has `bool held[16]` + `bool pressed[16]` per player. `InputState` has `PlayerInput players[4]` + `bool quit_requested`. Total semantic size: ~130 bytes, but packs to much less.

**Changes:**
- Add `serialize_input()` / `deserialize_input()` functions
- New file: `src/gameplay/input_state_net.cpp`, header: `include/openglad/gameplay/input_state_net.h`
- Pack bools as bitfields: 4 players x 32 bits (16 held + 16 pressed) = 16 bytes + 1 byte header (protocol version + quit flag)
- Add to `OG_SIM_SOURCES` in CMakeLists.txt

**Verify:** Unit test — round-trip serialization with various button combinations. Verify `move_x()` and `move_y()` helpers produce same results after round-trip.
