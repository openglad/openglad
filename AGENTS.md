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
| PR/branch definition of done: riding CI, completion reports, pre-merge sweeps, web previews | [.claude/skills/openglad-pr-workflow/SKILL.md](.claude/skills/openglad-pr-workflow/SKILL.md) |
| Test and coverage honesty: denominator rules, banned assertions, hang traps, parity goldens | [.claude/skills/openglad-test-integrity/SKILL.md](.claude/skills/openglad-test-integrity/SKILL.md) |

The skill files are plain markdown playbooks — they assume no Claude-specific
tooling. Follow their checklists and gates exactly; the hard invariants
(byte-identical parity via `og_test_parity`, coverage floors, vendor-header
isolation) are enforced by CI and scripts under `scripts/`.

## Environment and toolchain

Every build/test/emcc/ctest command runs inside the nix dev shell:
`nix develop --command bash -c '...'` (add
`--extra-experimental-features 'nix-command flakes'` if nix refuses).
Mandatory first command of any build task:
`nix develop --command bash -c 'which cmake c++ emcc'`.
A tool missing from PATH means you are OUTSIDE the shell, not that the
tool is absent. Never source ~/emsdk, never apt/snap-install a tool,
never patch a build script around a non-flake toolchain — if a tool is
genuinely missing, `grep -n <tool> flake.nix`, then add it to flake.nix
and say so. Never run `cmake -S . -B .`: an in-source configure drops a
root CMakeCache.txt pinning /usr/bin/cc that poisons every later
configure — if the wrong compiler appears, delete the root
CMakeCache.txt/CMakeFiles/ first. Use only the presets.

## Secrets

Never print, cat, head, grep, or Read the contents of `env`, `.env`,
`*secret*`, `*token*`, `*credential*`, or anything under ~/code/squad/.
"Use the keys in `<file>`" means `set -a; . <file>; set +a` inside one
non-echoing bash call, or `grep -oE '^[A-Z_]+=' <file>` to learn
variable NAMES only. Never echo a variable sourced from such a file. To
identify an unknown file use `file`/`wc -l`, not `cat`.

## Autonomy and delegation

Never stop at a partial checkpoint or ask "should I keep going" — the
answer is always yes. A turn ends when the whole named task is done and
its gates are green, or with a genuinely blocking question asked the
moment it is discovered. On low context, write a handoff note and keep
going.

There is no such thing as a "pre-existing test failure" or "pre-existing
flakiness": every failing test in a run you own is yours to fix, and the
whole suite passes at all times, unless the user explicitly waives a
specific failure.

Delegation default (do not make the user restate it): design, level and
scenario authoring, briefing prose, and hard debugging go to the
stronger model; mechanical implementation, cleanup, and audits go to the
cheaper one. On usage-limit exhaustion: checkpoint to a resume note and
report — never silently downgrade the model mid-task.

When proposing a mechanism (dependency pin, wrapper removal, migration),
apply it to EVERY member of the class in the same change; "and leave X
as-is" is not an option. Prefer deleting a first-party wrapper over
keeping it when callers can use the upstream API directly.

## Cross-tool handoff

Every non-trivial multi-session branch keeps a gitignored
`build/handoff.md` (or pinned PR comment): current goal, done, verified
(and how), known-broken, exact next command. Update before yielding.
Transcripts: codex at ~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl
(keyed by session_meta.cwd), Claude under ~/.claude/projects/.

## PR screenshots and proof media

Never commit screenshots, GIFs, or other PR/issue proof media to this repo
(no `docs/media/`, no images anywhere in the tree). Push them to
[openglad/openglad-screenshots](https://github.com/openglad/openglad-screenshots)
— one `pr-<number>/` directory per PR — and embed them in the PR body with
raw URLs pinned to a commit SHA:

```
https://raw.githubusercontent.com/openglad/openglad-screenshots/<sha>/pr-123/before.png
```

The capture tooling stays here (`scripts/media/capture_showcase.sh`,
`openglad_demo` frame dumps, `save_screenshot()`); it writes to the
gitignored `build/media/`, and finished artifacts get committed to the
screenshots repo.

Before any merge: run the pre-merge sweep in
[.claude/skills/openglad-pr-workflow/SKILL.md](.claude/skills/openglad-pr-workflow/SKILL.md)
— restore any dropped Gladiator-era signed/nostalgic comments (they move
with ported code, even cross-language) and delete agent-facing planning
markdown from the tree.

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
| A complete pack to copy (one declaration + art, runnable) | [api-reference → Worked example](docs/modding/api-reference.md#worked-example-a-complete-class-pack), files in `docs/modding/examples/emberwisp/` |
| The `og.family` schema — every key, per order | [design doc §4](docs/lua-classpacks-design.md) |
| Glyphs, radar colours, editor labels, pack-shipped sprites, animation sets | [design doc §4](docs/lua-classpacks-design.md), "Art, animation, and presentation" |
| Why my family id does not resolve | [design doc §5](docs/lua-classpacks-design.md) — declared `id` matches first; `name` is the compatibility fallback |
| Level and per-entity scripting | [design doc §7](docs/lua-classpacks-design.md), [api-reference → Level scripts](docs/modding/api-reference.md#level-scripts) |
| Where script errors and `og.log` output go, and how they are bounded | [api-reference → Script errors and logging](docs/modding/api-reference.md#script-errors-and-logging) |
| How to override a core family from a mod pack | [SKILL.md](.claude/skills/openglad-modding/SKILL.md), "Shipping a family that replaces a core one" |
| Real code to copy | `packs/core/families/living-00-soldier.lua` (behavior and declaration in one file), `tools/concept_mapgen/showcase_pack.cpp` (level scripting) |

When documents disagree, the source wins:
`src/gameplay/script/bindings_entity.cpp` and `world_scripts.cpp` for the
API, `src/gameplay/script/family_decl.cpp` and `src/resources/packs.cpp` for
the pack schema. A doc that disagrees with them is a bug worth fixing in the same
change.
