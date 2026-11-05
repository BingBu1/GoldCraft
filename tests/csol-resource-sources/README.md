# Reviewed HUD atlas source selection

Run `python tests/csol-resource-sources/test_sources.py -v` from the project root.

`tools/Prepare-CSOLResources.py` keeps the collector's model and material graph rooted at the supplied directory. Only independent `sprites/640hud<digits>.spr` atlas leaves use the verified extracted mount order. This prevents a later source selection from silently changing a model whose dependencies were already collected.

If the merged `common` contains a known wrong atlas and the correct mounted directory is absent, pass `--source-overrides reviewed.json`. Each override binds the original relative resource name, the selected current base's exact size/SHA-256, and the reviewed replacement's exact size/SHA-256:

```json
{
  "version": 1,
  "overrides": [
    {
      "relative": "sprites/640hud6.spr",
      "source": "reviewed/cstrike_chn/sprites/640hud6.spr",
      "bytes": 66366,
      "sha256": "de24e71db6cc9cff2e8bc27f0d3a8c8d3ad151a74ce994ea371e3af40645ec2d",
      "expectedBaseBytes": 66366,
      "expectedBaseSha256": "e9b6481c089b0cd4d9f04eaab2ea0b014499fe46a8019f7f47a5aaf89ae3d79b"
    }
  ]
}
```

The replacement path is relative to the override manifest unless absolute. These identities describe one verified CSO version; they are not permission to overwrite a later atlas. Changed bases, changed candidates, unused overrides, ambiguous duplicate entries and model overrides fail before resources are written. Existing different destination assets remain protected by the preparation tool's existing refusal rule. The manifest records both resolved source identities.

The ten tests cover mount priority, preservation of model graph roots, candidate/base identity checks, relative binding, unused overrides, read stability and path escape. Actual SKULL-3 preparation additionally verifies the complete collector closure and the three HUD6 death-icon rectangles; the source-selector tests do not claim gameplay or visual acceptance.
