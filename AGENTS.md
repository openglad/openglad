# Agent Guide (Codex, Claude, and other coding agents)

Start with [CLAUDE.md](CLAUDE.md) — build commands, module structure,
testing rules, and architecture pointers all live there and apply to every
agent, not just Claude.

## Domain guides

| Topic | Read |
|-------|------|
| Modding: class packs, Lua scripting, new classes/objects, level scripts | [.claude/skills/openglad-modding/SKILL.md](.claude/skills/openglad-modding/SKILL.md), then [docs/lua-classpacks-design.md](docs/lua-classpacks-design.md) (§3 determinism cookbook is LAW) and [docs/modding/api-reference.md](docs/modding/api-reference.md) |
| Campaign/level design, mapgen tooling, balance | [.claude/skills/openglad-campaign-design/SKILL.md](.claude/skills/openglad-campaign-design/SKILL.md) |
| Picker/menu UI changes (buttons, subscreens, labels, menu tests) | [.claude/skills/openglad-menus/SKILL.md](.claude/skills/openglad-menus/SKILL.md) |

The skill files are plain markdown playbooks — they assume no Claude-specific
tooling. Follow their checklists and gates exactly; the hard invariants
(byte-identical parity via `og_test_parity`, coverage floors, vendor-header
isolation) are enforced by CI and scripts under `scripts/`.
