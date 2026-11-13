# Cry of Fear dynamic shot decals

This narrow consumer implements the original `customdecal` message's dynamic
`shot` group. It does not implement other groups or static decals. It uses the
actual MetaHook SDK world surfaces and a real compatibility OpenGL backend.

The private original client is SHA-256
`d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`, PE32 x86,
image base `0x10000000`. Original addresses are VAs: registration `10058F70`,
message `10059450`, insertion `10057DA0`, resource parser `10059110`, ring
`10057950`, clip `100579C0`, basis `10058E40`, intersection `10058D20`, draw
`100587D0`, draw wrapper `100586C0`, reset `100586A0`, RGB TGA loader `10069630`,
mip reduction `10069F50`. The server writer `10117FE0` belongs to original
`hl.dll`, SHA-256
`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.
Original binaries, resources, IDA output and byte windows remain private.

Run directly from Git Bash with one compiler process:

```bash
python tests/cryoffear-custom-decal/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --sample analysis/cof-weapons-20261111/client-evidence/samples/client.dll \
  --output build/cof-custom-decal-20261112
```

The runner compiles only this consumer and its fixture using the pinned x86
Clang C++20/O3/ThinLTO/AVX2/precise toolchain and real SDK. It saves source hashes
before/after and the Git revision, without source snapshots. It exercises wire
validation, the 256-message queue, one random variant call per insertion, 1024
ring retention, strict overlap, samples/sky/turbulent eligibility, the first
model surface range, old/HL25 strides and reset lifecycle.

The differential test executes the original instructions in Unicorn and checks
exact float bits and polygon vertex order. It covers basis, intersection,
clipping, manual mip levels and the full original draw function's vertices/UVs.
A hidden real WGL context verifies texture upload, actual multiplicative pixels,
surface visibility, state/bindings/texture-matrix restoration and GL release.
This is not gameplay or real game performance evidence.

Integration ownership:

- Cold-load the original `gfx/decals/shot1.tga` through `shot5.tga` through the
  resource owner, then `InitializeGL` on the owning render thread/context.
- `PrepareMap(world, hl25)` copies eligible world polygons and planes. It must
  run after real world geometry is ready and again when that geometry changes.
  Cached polygons assume immutable world geometry for that preparation epoch.
- Pass the message body to `Receive(bytes, gl_customdecals, random)`. The random
  adapter must call the real engine's inclusive `RandomLong(0,4)` once per
  message when the world exists. `FlushPending` replays queued messages after
  world preparation, including before the first draw.
- Draw at the real opaque decal stage with matching depth, visibility stamp and
  original color space. The host must verify these conditions. False flags
  explicitly refuse the draw. Four fixed-function texture units are required.
- `ClearMarks` follows the original count/ring reset. Call `ClearMap` before
  freeing/replacing world geometry or changing authenticated sessions; this host
  adaptation also discards pending messages. Death/holster do not clear marks.
- `ReleaseGL` must precede owner-context destruction. Wrong-thread/context
  release refuses. `AbandonGL` is for an already lost context.

Host adaptations are bounded wire validation, 64-source-vertex/68-clipped-vertex
capacity, unsupported-group/static rejection, copied immutable map geometry,
cached clip/UV meshes, owned GL texture IDs and complete host GL state restore.
Insertion scans eligible surfaces and performs the original overlap scan.
Draw allocates no memory and performs no resource I/O or clipping; GL state is
queried once per draw call, never once per decal. Full weapon/resource/scene
integration, actual server-to-client delivery, map transitions and same-view
live CPU/frame-time measurements remain the parent task's acceptance work.

The actual adapter now lives in
`native/csol/client/weapons/guest_cof_decals.*`. It installs the unique
`guest::cof::resources::CustomDecalMessage` (`CoFDecal`) consumer, calls the real
engine RNG, and authenticates connection generation and owner thread. Its cold
resource preparation is called directly by the existing single signon listener
in `guest_cof_resources.cpp`, after actual Glock sound registration admission;
ordinary servers do not open these files. Five namespaced TGA bytes are retained
for cold video reconstruction. The server's existing Glock resource owner
preflights and precaches the five generic downloads, preserving returned IDs.
The resource candidate tool supports `--component shot-decals` for a five-file
SHA-pinned dry run without examining/copying unrelated resources.

`guest_cof_scene` owns the actual Renderer cold-world and NormalTriangles frame
contract. `PrepareGuestCofDecalMap` is cold and runs once per world epoch;
`DrawGuestCofDecals` accepts the actual world identity and frame contract. The
optional `visibleSurfaceWords` bitset uses global world surface indices and
takes precedence over the original engine stamp. It never writes engine stamps.
`ClearGuestCofDecalMap` preserves admitted textures because a first-level scene
clear can occur after signon. Actual connection/resource retirement uses
`InvalidateGuestCofDecalResources`; video release precedes native HUD_VidInit and
the resource owner reconstructs textures afterward. No file read, upload or map
preparation occurs in Draw.

The runner now links the real adapter and real single-listener resource owner.
Controlled audio/particle providers keep that test focused on the new decal
closure. Hidden WGL checks cover actual texture preparation, namespaced message
delivery, PVS index65, first-level clearing after signon, duplicate signon,
generation/reentrant retirement, foreign-thread rejection, cached video rebuild
and missing TGA rejection. It also checks sampler0 restoration when supported.
These remain controlled boundary tests, not a running GoldSrc client/server.

The separate server test compiles the actual ReGameDLL resource implementation
and checks only the new five downloads: exact ordered zero-based IDs, each
missing file before any precache, and each engine refusal/revocation leaving the
published output unchanged:

```bash
python tests/cryoffear-custom-decal/server_downloads.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-custom-decal-server-20261112
```
