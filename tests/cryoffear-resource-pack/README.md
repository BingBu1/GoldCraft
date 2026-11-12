# First-pistol namespaced resource candidates

This tool supports the reviewed P345, Glock and independent shot-decal lists.
Select `--component glock-and-shot-decals` for Glock; the default is P345 plus
shot decals. It creates a dry-run plan first;
`--write` requires that exact plan's SHA256. All outputs must be fresh paths
strictly under this workspace's `build` directory, with no link/junction/reparse
ancestor. It never deploys to a sandbox, changes an installation, modifies an
original resource, or creates a hard link to an external file.

The following paragraphs retain the original P345 checkpoint history. The current
P345 pack contains 41 primary weapon resources, five shot decals and one inherited
texture companion (47 files). The Glock pack has 44 weapon resources, five decals
and one companion (50 files). Both include the actual original `flash1.mdl` and
`gunsmoke.spr` used by client `1004BC30` and `10090130`; no effect substitutes or
hot model loads are introduced. Glock preserves all twelve Studio5004 events.
The current immutable catalogue is `native/guest/cof/resource_catalog.hpp`.
The current runner checks both packs and all refusal/rollback paths.

The before gate reuses the accepted resource05 identities: original x86 hl.dll
SHA256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`,
the rechecked 33 IDA windows/17013 bytes, original Precache1005EB90 and aim/
metadata consumers1005DDB0/1005E090/100DFF70. The authoritative six path groups
in `guest_cof_resources.hpp` must exactly match the frozen39 path/SHA pairs.
The original police P file is copied unchanged; grip acceptance remains false.

The exact current Studio5004 consumer checks for `weapons/`, splits at the first
slash and prepends `weapons_new/`. Prefixing the MDL options with a host namespace
would change the resulting logical key. The tool retains the complete V-model
bytes and its seven original sound events, records each local/remote namespaced
asset mapping, and refuses an enabled automatic server sound scan. It emits no
public `sound/weapons/...` aliases. Declaring `--server-model-sounds disabled`
records a required deployment policy; it does not verify a live server cvar or
admit the client audio backend.

The current ReHLDS `PrecacheModelTexture` replaces `.mdl` with `T.mdl` when
textureindex is0. The explicit inherited `models/w_9mmclip.mdl` has that layout,
so its real5096-byte `w_9mmclipT.mdl` is mandatory. Its SHA256 is
`18ce87d7f1e23f69f479a99a2e5c49ee5ea95e2712ade3b4c6496fbc172bae3d`.
The pack therefore has39 primary resources plus this one dependency. Missing
CoF loose clip data requires `--inherited-game-root`; no automatic directory
fallback is attempted. Both resources must come from the same explicit root.
The existing engine dependency service will request the namespaced companion.
Other external texture/group dependencies and special model dependency paths
are rejected. No extra model sidecar is emitted or admitted.

The unchanged Aurora script retains its original `sprites/fog2.spr` logical
reference; the existing prepared sprite consumer maps it to
`sprites/guest/cof/fog2.spr`. The manifest records that mapping, without claiming
particle preparation. Metadata icons and full inventory/backend behavior are
outside this fixed cold-resource list.

Run directly in Git Bash, using the read-only original game resource root:

```bash
python tools/Prepare-CryOfFearResourcePack.py \
  --original-game-root "$COF_RESOURCE_ROOT" \
  --inherited-game-root sandbox/cs-server/Half-Life/valve \
  --output build/cof-p345-pack/candidate --plan build/cof-p345-pack/plan.json \
  --server-model-sounds disabled
```

Inspect the plan, then repeat the same arguments with
`--write --expected-plan-sha256` and the printed hash. Each asset is independently
copied through an atomic temporary file; only after source and consumer hashes
are checked again is the complete candidate directory renamed into place.
Existing destinations are rejected. A generated manifest records hashes,
sources, inherited provenance, independent copies and remaining acceptance.

The real-resource test runner is:

```bash
python tests/cryoffear-resource-pack/run.py \
  --original-game-root "$COF_RESOURCE_ROOT" \
  --inherited-game-root sandbox/cs-server/Half-Life/valve \
  --output build/cof-resource-pack-test-20261111-01
```

Its receipt records CLI success/rejection cases and before/after hashes. It
checks actual output bytes/file identities, explicit inheritance, corrupt or
missing resources, plan changes, namespace policy, fresh workspace targets,
real Windows junction rejection and interrupted atomic preparation. It does
not run a game or accept grip, audio, particles, full weapons or performance.

The first real-resource run, build/cof-resource-pack-test-20261111-01, passed118 checks and19 CLI cases with51 inputs unchanged. It generated exactly40 independent assets (3787530 bytes) plus the candidate manifest. It exercised a real Windows junction and an interrupted second-file write; both left the final target absent and preserved inputs. The final frozen documentation rerun is build/cof-resource-pack-test-20261111-02/receipt.json. The earlier standalone probe produced only a dry-run plan and no candidate; its first refusal exposed the real inherited texture dependency, which was added from the exact existing engine rule.

The companion-closure follow-up also requires the header P345ClipTexture path pair to match the candidate dependency. This preserves39 primary paths plus one dependency and keeps candidate SHA/size pins in the pack tool only. Production VFS admission validates structure, allowing legitimate complete texture updates. The final follow-up rerun is build/cof-resource-pack-test-20261111-03/receipt.json.
