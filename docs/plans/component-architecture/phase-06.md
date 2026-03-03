# Phase 6: Move gloader to resources

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 5](phase-05.md)
**Followed by:** [Phase 7](phase-07.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Goal

Move the entity factory (`gloader`) out of `LevelData` into the
resources layer. `GameWorld::add_ob()` uses a factory callback instead of
owning a loader directly. This establishes clean gameplay-layer dependencies
from the start.

Since Phase 4 already introduced `current_game` and eliminated `sim_*` pointer
wiring, the factory callback does NOT need transitional entity wiring logic.
Newly created entities use `current_game->` from the start.

## The gloader Challenge

`gloader` is a factory that touches all layers:
- Reads pixel data from disk → **resources**
- Creates walker objects → **gameplay** types
- Attaches render components (pixieN) → **interface** types
- Populates stats from family descriptors → **gameplay**

## Solution

gloader lives in resources and uses a factory/callback pattern:

```cpp
// resources layer
struct EntityFactory {
    // Provided by platform at setup time
    std::function<void(walker&, const PixieData&)> attach_render;
    // nullptr for headless — walkers get no render component
};

class loader {
public:
    loader(EntityFactory factory);
    std::unique_ptr<walker> create_walker(Order, int family);
    // ...
};
```

**GameWorld side:**

```cpp
// Set by platform at setup time
std::function<std::unique_ptr<walker>(Order, int family)> entity_factory;

walker* GameWorld::add_ob(Order order, int family) {
    auto w = entity_factory(order, family);
    // insert into oblist, update living_count, etc.
}
```

## Animation Tables

The animation frame arrays (~23 animation tables + ~30 frame cycle arrays) in `gloader.cpp` are
authored in gloader but baked into entities at creation time via the `ani`
pointer. Entity code indexes `ani[curdir + ani_type * NUM_FACINGS][cycle]`
at runtime but never goes back to gloader. The tables stay with gloader
in resources — no cross-component dependency.

## `popup_dialog()` Layering Violation

`gloader.cpp:553` calls
`popup_dialog()` (a UI function) when entity creation fails due to missing
graphics. This must be resolved when moving gloader to resources. Options:
- Return a null/error and let the caller handle the dialog
- Use an error callback wired by platform

## `loader` Constructor Dependency

`loader` currently takes `LevelData*` in its constructor
(`explicit loader(LevelData* owner)`) and stores it as `owner_level`. This
pointer is used in `create_walker_owned()` to call
`owner_level->wire_entity()`. Phase 6 must eliminate this constructor
parameter entirely — the factory callback pattern replaces both the
`LevelData*` owner and the wire_entity call (since Phase 4 already
removed sim_* wiring in favor of `current_game`).

## Steps

1. Move `gloader` from `LevelData` to resources layer
2. Define `EntityFactory` callback struct for render component attachment
3. Add `entity_factory` callback on `GameWorld`
4. Platform wires gloader + attach_render callback at setup time
5. `GameWorld::add_ob()` calls `entity_factory` instead of `myloader`

## Risk

Medium — requires the callback plumbing. But this is a clean,
well-defined interface boundary.

## Testing

Full ctest. Verify entity creation works through the callback
in both SDL and headless modes.
