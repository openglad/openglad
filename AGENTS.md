# Agent Guide (Codex, Claude, and other coding agents)

Start with [CLAUDE.md](CLAUDE.md) — build commands, module structure,
testing rules, and architecture pointers all live there and apply to every
agent, not just Claude.

## Domain guides

| Topic | Read |
|-------|------|
| Modding: class packs, Lua scripting, new classes/objects, level scripts | [.claude/skills/openglad-modding/SKILL.md](.claude/skills/openglad-modding/SKILL.md) — then the modding map below |
| Campaign/level design, mapgen tooling, balance | [.claude/skills/openglad-campaign-design/SKILL.md](.claude/skills/openglad-campaign-design/SKILL.md) |
| Picker/menu UI changes (buttons, subscreens, labels, menu tests) | [.claude/skills/openglad-menus/SKILL.md](.claude/skills/openglad-menus/SKILL.md) |

The skill files are plain markdown playbooks — they assume no Claude-specific
tooling. Follow their checklists and gates exactly; the hard invariants
(byte-identical parity via `og_test_parity`, coverage floors, vendor-header
isolation) are enforced by CI and scripts under `scripts/`.

## Modding map

Three documents, three lanes: the skill is the playbook, the design doc is
architecture plus the determinism law, the API reference is the symbol table.
Everything a mod author needs is one hop from this table.

| I need... | Go to |
|---|---|
| How to build, stage, test and **prove** a mod dispatches | [SKILL.md](.claude/skills/openglad-modding/SKILL.md) |
| The rules my Lua must obey to stay deterministic (R1–R10) | [design doc §3](docs/lua-classpacks-design.md) — law, not advice |
| Naming, headers, comments, helpers, and legacy alias style | [lua-style.md](docs/lua-style.md) |
| Every `og.*` function, walker/stats/guy method, constant, hook signature | [api-reference.md](docs/modding/api-reference.md) |
| A complete pack to copy (classpack.yaml + .lua + art, runnable) | [api-reference → Worked example](docs/modding/api-reference.md#worked-example-a-complete-class-pack), files in `docs/modding/examples/emberwisp/` |
| The `classpack.yaml` schema — every key, per order | [design doc §4](docs/lua-classpacks-design.md) |
| Glyphs, radar colours, editor labels, pack-shipped sprites, animation sets | [design doc §4](docs/lua-classpacks-design.md), "Art, animation, and presentation" |
| Why my family id does not resolve | [design doc §5](docs/lua-classpacks-design.md) — declared `id:` matches first; `name:` is the compatibility fallback |
| Level and per-entity scripting | [design doc §7](docs/lua-classpacks-design.md), [api-reference → Level scripts](docs/modding/api-reference.md#level-scripts) |
| Where script errors and `og.log` output go, and how they are bounded | [api-reference → Script errors and logging](docs/modding/api-reference.md#script-errors-and-logging) |
| How to override a core family from a mod pack | [SKILL.md](.claude/skills/openglad-modding/SKILL.md), "Shipping a family that replaces a core one" |
| Real code to copy | `packs/core/scripts/soldier.lua` and its data half `packs/core/families/living-00-soldier.yaml`, `tools/concept_mapgen/showcase_pack.cpp` (level scripting) |

When documents disagree, the source wins:
`src/gameplay/script/bindings_entity.cpp` and `world_scripts.cpp` for the
API, `src/resources/classpack_yaml.cpp` and `src/resources/packs.cpp` for the
pack schema. A doc that disagrees with them is a bug worth fixing in the same
change.
