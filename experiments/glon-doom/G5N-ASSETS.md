# G5n corridor asset manifest

Reference used as art direction only:
`/mnt/c/users/anon_/downloads/corridor.png` (approved corridor reference).
No reference crops are used as textures.

All current materials are **temporary neutral solids** in `gl_corridor()` and
`gl_prop_3d()`. They are greybox placeholders, not final artwork.

| Asset | Purpose | Resolution | UV/tiling | Alpha | Placement |
|---|---|---|---|---|---|
| `wall_bulkhead_steel` | main corridor wall panels | 1024×1024 | world-scale U/V, seamless | opaque | side walls, bulkhead |
| `wall_recess_panel` | recessed panel faces | 512×512 | aligned to panel segments | opaque | side-wall panels |
| `rib_structural` | projecting structural ribs | 256×1024 | vertical, seamless | opaque | wall/ceiling/floor ribs |
| `service_housing` | lower-wall utility housings | 1024×512 | horizontal run, seamless | opaque | lower walls |
| `floor_plate` | deck plates | 1024×1024 | 1 plate per 256 units, seamless | opaque | floor |
| `floor_grate` | grate bars/drainage | 512×512 | aligned bars, seamless both axes | cutout/alpha | floor strips |
| `ceiling_panel` | dark ceiling infill | 512×512 | grid-aligned | opaque | ceiling between ribs |
| `light_lens` | recessed fixture lens | 256×128 | no tiling | emissive | ceiling fixtures |
| `pipe_metal` | longitudinal pipe runs | 256×256 | U along pipe, seamless | opaque | upper wall corners |
| `conduit_red` | secondary conduit bands | 128×128 | U along run | opaque | ribs/pipe joints |
| `bulkhead_door` | near/far bulkhead door | 1024×1024 | centred panel, hazard strip geometry | opaque | door panels |
| `cargo_crate` | crate faces | 512×512 | per face, seamless edges | opaque | crate cover |
| `cabinet_metal` | machinery cabinet | 512×1024 | front/side faces | opaque | cabinet |
| `drum_painted` | drum side/end | 512×512 | cylindrical U/V | opaque | drums |

Required final artwork must preserve dark weathered metal, recessed panels,
rib/shadow structure, warm auxiliary lights, floor grates and believable grime.
No placeholder textures should be promoted as final assets.
