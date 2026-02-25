# Key Type Changes

**Part of:** [Component Architecture Plan](README.md)
**See also:** [Target Architecture](target-architecture.md)

---

## GameWorld (new — gameplay component)

**Replaces `LevelData` entirely.** GameWorld absorbs all gameplay-relevant fields
from both `screen` and `LevelData`, plus the tick logic from `legacy simulation layer`.
`LevelData` and `legacy simulation layer` both cease to exist as classes. Rendering data
(`renderer_`, `pixdata[]`, draw offsets) moves to a new `LevelVisuals` type
owned by `screen` in the interface layer.

```cpp
// include/openglad/gameplay/game_world.h
namespace og::gameplay {

class GameWorld {
public:
    // Level metadata
    int id;
    std::string title;
    short par_value;
    short time_bonus_limit;
    char type;                            // level flags (TYPE_CAN_EXIT_WHENEVER, etc.)

    // Entity storage (currently in LevelData)
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> dead_list;  // dead entities kept for pointer validity
    int living_count;                     // count of Order::Living entities only (was numobs)

    // Spatial data
    std::unique_ptr<obmap> myobmap;
    PixieData grid;                       // tile type grid (gameplay uses for passability)
    smoother mysmoother;                  // terrain type grid + genre queries
    std::int32_t pixmaxx, pixmaxy;

    // Simulation state (absorbed from legacy simulation layer)
    SimRandom rng_;                       // deterministic LCG
    std::uint32_t tick_count_ = 0;        // total ticks across all levels
    std::uint32_t level_tick_count_ = 0;  // ticks in current level

    // Game state flags (currently on screen / TickResult)
    short level_done = 0;                 // 0=foes remain, 1=no foes+exits, 2=auto-advance
    bool game_ended = false;              // true when game over condition reached
    short next_level = -1;               // level to transition to (-1=none)
    short ending = 0;                     // 0=win, 1=loss, SCEN_TYPE_SAVE_ALL=failure
    std::int32_t enemy_freeze = 0;
    signed char timer_wait = 6;
    char end = 0;
    bool retry = false;
    float control_hp = 0;
    bool withdraw_requested = false;    // set by WithdrawToLevel event; tick() early-outs
    bool create_hit_effects = true;    // gates FX entity creation in combat (from cfg hit_anim)

    // Per-team score accumulator (read back by outer layer after level ends)
    std::uint32_t m_score[4] = {};

    // Gameplay-relevant settings (populated by outer layer before level)
    short my_team = 0;
    unsigned char allied_mode = 0;
    short current_scenario = 0;
    std::set<int> completed_levels;
    short difficulty = 100;               // enemy stat multiplier — resolved from difficulty_level[current_difficulty_] (values: 50, 100, 200)

    // Entity factory (wired by platform at setup time, backed by gloader in resources)
    std::function<std::unique_ptr<walker>(Order, int family)> entity_factory;

    // Queries
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    // ... other finders ...

    // Entity creation (uses entity_factory callback)
    walker* add_ob(Order order, int family);
    walker* add_fx_ob(Order order, int family);
    walker* add_weap_ob(Order order, int family);

    // Tick (absorbed from legacy simulation layer — updates level_done, entity lists, etc.)
    void tick(SimEventLog& events);
};

} // namespace og::gameplay
```

**Key design decisions:**

- **`legacy simulation layer` is absorbed.** The tick logic, tick counters, and RNG all live
  directly on GameWorld. `legacy simulation layer` as a separate class is deleted.
- **`smoother` stays whole in GameWorld.** Entity code needs
  `query_genre_x_y()` for gameplay decisions (forest walk, weapon visibility,
  door orientation). The `smooth()` method is editor-only but keeping it here
  avoids splitting a small class. The smoother is a terrain type grid, not a
  rendering interpolator. Note: `smooth()` uses `ctx().rng` for tile variant
  selection — this is NOT migrated to `current_game->world->rng_` because
  it only runs in the editor, never during gameplay ticks. See Phase 4 notes
  on non-migrated `ctx().rng` call sites.
- **`living_count` replaces `numobs`.** Tracks only `Order::Living` entities,
  not all objects in `oblist`. Used by generators and slimes for spawn caps.
  Renamed for clarity.
- **`type` field** holds level flags (`TYPE_CAN_EXIT_WHENEVER`,
  `TYPE_MUST_DESTROY_GENERATORS`, etc.) used by tick logic and exit treasure.
- **`difficulty`** is the resolved enemy stat multiplier (50, 100, or 200),
  looked up from `difficulty_level[current_difficulty_]` (defined in
  `src/ui/picker_common.cpp`) where `current_difficulty_` is an index (0, 1, 2)
  on `GameSession`. The outer layer resolves this index to the multiplier value
  and populates `GameWorld::difficulty` before level start. Entity code in
  `living.cpp:526-537` currently does this lookup inline — after migration it
  reads the pre-resolved value from `current_game->world->difficulty`.
- **No `GameplayConfig`.** Most `cfg_store` effect settings (`attack_lunge`,
  `hit_recoil`, `damage_numbers`, `hit_flash`, `heal_numbers`) do not affect
  `worldx_`/`worldy_` or damage calculations. Currently the config gates whether
  sim code sets these fields on walker at all (e.g.
  `if (sim_config->is_on("effects", "attack_lunge")) attack_lunge = ...`).
  **Migration approach:** sim code sets these fields unconditionally (remove
  the config gates), and the interface layer checks cfg before reading them
  for display. The fields accumulate even when visually disabled — this is
  harmless since they're only consumed by render code. The `sim_config`
  pointer is removed from `SimEntity`.
  **Exception — `hit_anim`:** At `walker_combat.cpp:90-96`, the `hit_anim`
  config gate controls creation of FX entities (`add_ob(Order::FX, FAMILY_HIT)`).
  This is NOT rendering-only — it creates entities that appear in entity lists,
  affecting gameplay (entity counts, iteration, potential performance). Removing
  this gate unconditionally would change behavior. Instead, a
  `bool create_hit_effects` flag on GameWorld (see field list above) is populated
  from the config by the outer layer. Sim code checks
  `current_game->world->create_hit_effects` instead of `sim_config->is_on()`.
- **No `special_name`/`alternate_name` tables.** These were redundant copies of
  data already in the family descriptor registry. Entity code fetches names
  directly from `get_family_descriptor(family)->special_names[index]`.
  Note: `special_name[NUM_FAMILIES][NUM_SPECIALS]` on `screen` and
  `special_names[FD_NUM_SPECIALS]` on `FamilyDescriptor` are separate arrays.
  Entity code (35 family files in `src/entities/families/`) already uses
  `get_family_descriptor()`. The `screen` arrays are consumed by UI code
  (`view.cpp`, `score_panel.cpp`, `input_event_bridge.cpp`, etc.). Consumer
  migration needs to route UI code to `get_family_descriptor()` directly.
- **No `description` field.** Level description text is UI-only — loaded from
  the level file by resources and handed to the interface layer for display.
  Note: `LevelData::description` (`std::list<std::string>`) is currently loaded
  by `load()` and saved by `save()`. The resources layer's `load_level()`
  function must return/populate the description separately from GameWorld,
  passing it to the interface layer for display.
- **`dead_list`** grows throughout a level and is only cleared when the outer
  layer calls a cleanup method at level end (`delete_objects()`). Stale pointer
  cleanup (nullifying `foe_`, `owner_`, etc.) happens during the tick before
  entities move to `dead_list`.
- **`level_done` and other tick results** are stored on GameWorld and written
  directly by `tick()`. The previous `TickResult` return pattern is replaced —
  tick updates `level_done`, `game_ended`, `next_level`, and `ending` in place.
  The outer layer reads these fields after each tick to drive level transitions.
- **Entity factory callback** (`std::function`) is wired by platform at setup
  time, pointing to `gloader` in the resources layer. `GameWorld::add_ob()`
  calls it to create entities mid-tick (generators spawning enemies, slimes
  duplicating, etc.). Headless mode provides a minimal factory.
- **Pathfinding state** (`path_walker`, `path_map`, `pather` thread-locals in
  `walker_pathing.cpp`) moves onto **GameplayContext**, not GameWorld. Reasons:
  `MicroPather` has internal pointers and is not trivially moveable — putting it
  on GameWorld would make GameWorld non-moveable. These are scratch computation
  state, not game state (no serialization needed). Moving to GameplayContext
  eliminates the `path_walker` thread-local hack — `Map::AdjacentCost()` can
  access `current_game->world->query_grid_passable()` and
  `current_game->world->myobmap->obmap_get_list()` directly instead of going
  through a thread-local walker pointer. `pather.Reset()` is still needed per
  call (graph validity, not storage location). See GameplayContext struct below.
- **Sound is already event-driven.** Entity code uses `emit_sound(sim_events,
  SOUND_ID)` to push `PlaySound` events into `SimEventLog`. The outer layer
  drains these after each tick. No direct audio calls exist in entity code —
  no migration needed.

---

## LevelVisuals (new — interface component)

Holds the rendering data that was previously on `LevelData`. Named
`LevelVisuals` (not `LevelRender`) to avoid confusion with the existing
`LevelRender` class that it wraps:

```cpp
// include/openglad/interface/level_visuals.h
struct LevelVisuals {
    PixieData pixdata[PIX_MAX];          // tile sprite graphics
    std::unique_ptr<LevelRender> renderer_;
    std::int32_t topx, topy;             // draw offset
};
```

---

## screen (refactored — interface/platform layer)

**What stays in `screen`:**
- `video` base class (pixel buffer, SDL surface, draw primitives)
- `viewob[5]` (`std::unique_ptr<viewscreen>` — camera viewports)
- `numviews` (current viewport count)
- `redraw()` and the rendering pipeline
- `soundp` (`std::unique_ptr<soundob>` — audio subsystem)
- `newpalette`, `palmode` (palette/rendering state)
- `framecount`, `timerstart` (frame timing)
- `redrawme` (redraw flag)
- `special_name[NUM_FAMILIES][NUM_SPECIALS]`, `alternate_name[NUM_FAMILIES][NUM_SPECIALS]` (UI display names — consumed by `view.cpp`, `score_panel.cpp`, `input_event_bridge.cpp`; eventually routed to `get_family_descriptor()` directly)
- `LevelVisuals level_visuals_` (rendering data extracted from old LevelData)

`screen` holds a `GameWorld*` (non-owning — platform owns the world) and
delegates game logic to it.

**Ownership transfer:** During Phases 1–3, `screen` temporarily owns GameWorld
as `GameWorld world_;` (a direct member). In Phase 10, when screen moves to the
interface layer, GameWorld ownership transfers to platform (`GameSession`).
`screen` is refactored to hold a non-owning `GameWorld*` pointer instead.

---

## GameplayContext (new — gameplay component)

```cpp
// include/openglad/gameplay/gameplay_context.h
namespace og::gameplay {

struct GameplayContext {
    GameWorld*    world = nullptr;
    SimEventLog*  sim_events = nullptr;

    // Pathfinding scratch state (eliminates thread-local hack in walker_pathing.cpp)
    std::unique_ptr<PathfindingState> pathfinding;  // owns Map + MicroPather
};

extern thread_local GameplayContext* current_game;

} // namespace og::gameplay
```

`mounted_campaign` is NOT part of GameplayContext — gameplay has no need for the
campaign identifier. It stays at the platform layer (on `GameSession` / the
existing `GameContext`).

Replaces the per-entity `sim_*` pointer injection. Entity code accesses
`current_game->world`, `current_game->world->rng_`, etc. instead of per-walker
injected pointers. The RNG is accessed through `world->rng_` rather than a
separate pointer — having a redundant `rng` alias on the context would be a
stale-pointer footgun if the world is ever swapped. For tests that need a mock
RNG, `GameWorld::rng_` is an `IRandom` interface and can be swapped directly.

---

## IRenderComponent (new — gameplay component)

```cpp
// include/openglad/gameplay/render_component_base.h
namespace og::gameplay {

class IRenderComponent {
public:
    virtual ~IRenderComponent() = default;
    // Gameplay never calls methods on this. It just owns the lifetime.
    // The interface layer downcasts to the concrete type when rendering.
};

} // namespace og::gameplay
```

`walker` holds `std::unique_ptr<IRenderComponent> render_`. The current
`WalkerRender` (wrapping `pixieN`) extends `IRenderComponent` and lives in the
interface layer.
