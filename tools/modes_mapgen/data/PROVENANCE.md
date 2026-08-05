# Vendored campaign members for tools/modes_mapgen

Byte-copies of the shipped packages this campaign absorbs. The generator
reads these instead of the live `builtin/*.glad` files so the absorbed
packages can be deleted (Phase III of the game-modes plan) without breaking
regeneration.

- `arenas/` — extracted from `builtin/org.openglad.arenas.glad` as shipped at
  commit 6b24cc29 (branch `feature/mp-game-modes`): the six `scen/scenN.fss`
  entity layouts (N in 300-305) and the twelve migrated grid members
  `pix/scen03NN.png` + `pix/scen03NN_d0.png`. The package's legacy dead
  weight (`*.pix` grids, `icon.pix`, the empty `sound/` entry) is
  deliberately NOT vendored — the modes campaign drops it.
- `ctf/` — extracted from `builtin/org.openglad.ctf.glad` as shipped at the
  same commit: the ten `scen/scenN.fss` layouts (N in 500-509) and the
  eighteen grid members `pix/scen05NN.png` (+ `_d0.png` where the level
  ships a decor plane; 505/506 have none).

Extraction is a plain zip-member copy (no re-encode). The `pix/*.png`
members are re-emitted into `modes.glad` byte-identically;
the `.fss` files are LOADED through the production level reader (the
generator mounts each data directory as a PhysFS root), transformed
(mode re-dress, briefing re-voice, type byte), and re-serialized — entity
records are content-preserved, not byte-preserved.

Verify grid byte-identity against an absorbed package with:

```
python3 - <<'EOF'
import zipfile, hashlib
a = zipfile.ZipFile("builtin/modes.glad")
for n in sorted(a.namelist()):
    if n.startswith("pix/scen03") or n.startswith("pix/scen05"):
        print(hashlib.sha256(a.read(n)).hexdigest()[:16], n)
EOF
```
