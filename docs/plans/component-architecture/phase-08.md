# Phase 8: Define IRenderComponent in Gameplay

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 7](phase-07.md)
**Followed by:** [Phase 9](phase-09.md)
**Key types:** [IRenderComponent](key-types.md#irendercomponent-new--gameplay-component)

---

## Goal

`walker` holds a gameplay-defined interface base for its render
component. Concrete implementation stays in the interface layer.

## Steps

1. Create `include/openglad/gameplay/render_component_base.h` with
   `IRenderComponent` (virtual destructor only)
2. Change `walker::render_` from `std::unique_ptr<WalkerRender>` to
   `std::unique_ptr<og::gameplay::IRenderComponent>`
3. Update `WalkerRender` to extend `IRenderComponent` (in interface layer)
4. Interface rendering code downcasts:
   `static_cast<WalkerRender*>(w->render_component())`
5. Same pattern for `LevelVisuals` → `ILevelVisuals` base in gameplay
6. Remove `#include <openglad/entities/walker_render.h>` from walker.h
   (only forward-declare `IRenderComponent`)

## Note

This phase modifies `walker.h` in its current location
(`include/openglad/entities/`). The physical move to `gameplay/` happens in
Phase 10. This phase runs after Phases 4–5 because all three modify
`walker.h` / `SimEntity` — sequencing avoids merge conflicts.

## Decision: ILevelVisuals Not Needed

LevelVisuals is owned by screen (runtime layer), not by any gameplay type.
Gameplay code never references LevelVisuals. The ILevelVisuals interface
was designed for the case where gameplay needs to hold a type-erased
rendering reference, but this case does not arise for level visuals
(only for per-entity render components via IRenderComponent).

## Risk

Low — small, mechanical change. The hard part is making sure all
downcast sites are updated.
