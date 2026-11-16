# Client-managed eternal-night player lights

This is the user's custom gameplay extension. The server sends one 24-byte `CsoNLight` policy when the stage or persistent configuration changes, plus one replay on join. It creates no light entities and sends no light positions, player masks or frame-by-frame packets. Each client follows its own predicted origin and other current players' interpolated entity origins.

The one `addons/goldcraft/config/gameplay.json` contains `night_player_lights` with an enabled default and three values: `night_player_light_radius` (128 GoldSrc units), `night_player_light_brightness` (48/255), `night_player_light_color` (16777215, white). These are custom defaults. M's administrator page edits and atomically persists them through the existing permission/revision-checked settings service. They apply on the next server frame. The 96 KiB document limit covers every permitted 512-byte comment at its maximum escaping; it is not an unbounded file parser.

Run the production protocol/config and actual SDK client consumer:

```bash
python tests/csol-challenge-environment/run.py \
  --client-build build/csol-client-crc/client \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/night-player-lights-sdk-new \
  --settings-regression --night-lights
```

The output directory must be new. This reuses an existing real compiler cache without copying source. The client fixture controls only the Renderer boundary, engine entity provider and hook registration; it executes production message handling, phase/config lifecycle and frame selection. It verifies 32 players, prediction/interpolation, remote death without an unsent health field, stale PVS, spectators, respawn, map/connection cleanup, out-of-thread rejection and 2,000 moving frames without allocation. No gameplay/FPS claim follows from these checks.

Renderer's real `scene_lights_tests` executes the independent moving-pool factory, stable handles, 32-player updates, unchanged native dlight pool, real extracted view/PVS dispatch, rejection preservation and zero steady allocations. `scene_static_shadows_tests` executes the actual dynamic shadow fallback and verifies that world/opaque geometry remain included while the source player's body is excluded. Existing static SceneLights001 ABI stays unchanged. Its actual GL/GPU and multiplayer acceptance remains separate.

The actual GameDLL environment fixture adds `CsoNLight` checks to its phase, end, replay and late-join groups, including 50 real unchanged Think frames with zero additional light packets. Run it against a new full GameDLL link seal as described in [environment SDK validation](../csol-challenge-environment/README.md). Never treat a single-client sink, a compile or CPU timing as real multiplayer lighting acceptance.

The `environment-console` group verifies current-map CVAR edits independently
of the immutable saved JSON pointer. All four registered numeric variables are
read directly, with no string lookup, file parsing or player-light iteration on
the server. Only a changed valid policy causes a packet. Menu edits still save
to the same JSON and supersede temporary console overrides after publication;
invalid numeric values keep the previous valid policy and cannot prevent a
valid disable request.

CSOLSupport and Renderer must also be paired. The client requests
`MetaRendererMovingLights_API_001`; an older Renderer can leave the ordinary HUD
working while the player lights never prepare. Run
`tools/csol_resource_capabilities.py` with `--consumer`, `--candidate` and
`--renderer` before deployment. The sandbox deployment script and client package
planner reject the missing marker; the candidate planner can include an explicit
`--renderer` and its PDB. These are cold static checks, not proof that the live
factory, renderer or GPU effects initialized.
