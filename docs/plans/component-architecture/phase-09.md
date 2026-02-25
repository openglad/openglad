# Phase 9: Split the Data Layer

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 8](phase-08.md) (and [Phase 5](phase-05.md) for SaveData decoupling)
**Followed by:** [Phase 10](phase-10.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Goal

Separate gameplay data structures from file I/O serialization.
`LevelData` was already eliminated in Phase 7. `gloader` was already moved to
resources in Phase 6. This phase moves the remaining serialization code.

## Gameplay Keeps

- `GameWorld` (the in-memory game state — already exists from Phases 1–3)
- `guy` struct (character stat block)
- `statistics` struct
- Family descriptor types and registries

## Resources Gets

- Level file I/O: `load_level(path, GameWorld&)`, `save_level(GameWorld&, path)`
  (free functions that populate/serialize the gameplay-relevant fields)
- Save file I/O: `SaveData` struct + `load_save()`/`save_save()`
- `gparser` / `cfg_store` (config parsing)
- `pixie_data` loading from .pix files
- All PhysFS/zip/yaml code (already in `io`)

**(gloader already moved in Phase 6.)**

## Note on `grid_file`

`LevelData::grid_file` (`std::string`,
`level_data.h:114`) stores the source filename for the grid tilemap. It's a
resources concern — loaded from the level file, used by save. It does NOT
belong on `GameWorld` since it's a file path, not gameplay state. The resources
layer's `load_level()` / `save_level()` functions pass it alongside GameWorld
data during load/save (e.g. as a separate output/input parameter or in a
`LevelFileMetadata` bag).

## Steps

1. Move `SaveData` entirely to resources
2. Move `gparser`, `pixie_data` to resources
3. Move level file format code (`load_scenario_data()` etc.) to resources
   as free functions operating on `GameWorld&`

**Note:** `LevelData::load()` / `save()` currently serialize both gameplay data
(entity lists, grid, metadata) and visual data (pixdata[], grid_file). The
resources-layer `load_level()` must populate both `GameWorld` and
`LevelVisuals` (or return visual data separately). Similarly `save_level()`
must read from both. Consider a `LevelFileData` bag struct that holds
`GameWorld&` + `LevelVisuals&` + `LevelFileMetadata` (grid_file, description)
as the serialization interface.

## Risk

Low-Medium — straightforward moves. Smaller than originally planned
since gloader was already handled.

**Depends on Phase 5.** SaveData must be fully decoupled from entity code
before moving it to resources — otherwise you'd move a type that still has
entity-layer coupling and need to rework the boundary.
