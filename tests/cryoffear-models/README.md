# Cry of Fear player-model tools

`tools/Prepare-CryOfFearPlayerModels.py` validates hash-pinned original P models and prepares byte-identical copies. `tools/Generate-CryOfFearPlayerModel.py` constructs a new static P from explicitly selected V-model geometry. Neither tool chooses a grip or a character animation.

Generation requires a calibration JSON containing:

| Field | Required evidence/input |
|---|---|
| `schema` | Integer `1` |
| `status` | `candidate` or `verified`; review is the caller's responsibility |
| `evidence` | Existing evidence file `path` and full `sha256` |
| `source` | Full model `sha256`, explicit anchor `bone`, and `sequence` with integer `index`, matching `label`, integer `frame` and `blend` |
| `target` | Full original P-template `sha256`, `handBone` = `Bip01 R Hand`, `bone` = its direct child `SHOTGUN`, and explicit sampled `sequence` |
| `sourceAnchorToTargetBone` | Reviewed rigid 3×4 transform from the sampled source-anchor coordinates into the sampled target-bone coordinates; no default is provided |
| `textures` | Explicit list of material `name` and indexed-image-plus-palette `sha256` pairs |
| `sourceVertexBones` | Explicit list of bones allowed to contribute triangle vertices |
| `attachments` | `all`; all original attachment positions/vectors are transformed and retained |

Run generation with `--source-model`, `--template-model`, `--calibration`, `--output` and `--report`. Both output paths must be new files below the workspace `build` directory. An unreviewed calibration additionally requires `--candidate` and an output filename ending in `.candidate.mdl`. Model hashes, sequence labels, material hashes, rigid transforms, hierarchy, ranges and animation RLE are checked before resource output. Scale, reflection, controller-state guesses, missing materials, empty geometry and implicit external sequence groups are rejected.

The tool expands source commands to triangles for filtering, then groups compatible adjacent triangles into native strip/fan commands. It preserves winding, integer UVs, texture flags/pixels/palette, sampled transformed normals, all skin families of selected meshes, body alternatives and attachments. Normal spans remain contiguous per material for native Studio lighting. Removed body groups are recorded in `bodyMap`; any game code that controls `body` must account for that mapping. Geometry freezes into a one-frame looping `idle` on the original template skeleton. It does not implement moving reload parts or a weapon gameplay state machine.

The private assets and their calibration evidence are not committed. From Git Bash, run the real-asset tests with:

```bash
COF_PLAYER_INVENTORY=<private-original-P-inventory.json> python -B tests/cryoffear-models/test_prepare_player_models.py
COF_MODEL_INVENTORY=<private-model-inventory.json> COF_PLAYER_INVENTORY=<private-original-P-inventory.json> python -B tests/cryoffear-models/test_generate_player_model.py
```

Missing inventories fail. The generation tests replay axe/FAMAS/MP5 assets, verify every retained triangle corner, normal, UV, image/palette and attachment, exercise a nonzero FAMAS reload frame, and run the actual CLI plus rejection paths. Their explicitly declared identity-wrist fixture is **not a reviewed CS grip**. Test candidates remain in a unique build subdirectory and must never be deployed.

Numeric geometry checks and successful preparation do not establish hand contact, left-hand support, stock/CSO action suitability, gameplay, lifecycle correctness or frame-time performance. Those require separate review and real gameplay. All preparation runs offline; the tools add no rendering or input work to the game.
