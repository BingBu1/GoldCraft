# Glock owner with native CS movement

The production adapter keeps actual CS movement, including its jump penalty and optional native run behavior. It translates actual grounded/fully-ducked flags and command seeds, retains the original owner jump/edge phase order, and isolates native player lives. CoF sprint is unavailable in this explicitly selected host mode; the unrelated CS slime-immunity bit never becomes sprint. The original CoF stamina/stairs/sprint graph remains unsupported.

`NativeOwnerMode` requires the server to enforce free activity, unavailable phone mode, no substances, finite ammo and first-person view. The authority callback must revoke the binding if these invariants or session/map/life identity change. These are explicit compatibility restrictions, not inferred original defaults. First-draw/light state and authoritative weapon rules are supplied by the actual owner; the adapter does not infer them from another plugin's CS fields.

Run from the project root in Git Bash with the configured compiler environment:

```bash
python tests/cryoffear-native-owner/run.py \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-native-owner-check
```

The output directory must be new. The runner compiles the actual adapter with current SDK declarations as x86 C++20/O3/ThinLTO/AVX2/precise, records Git revision and compiler-discovered input hashes, and generates only narrow SDK test adapters under `build`. Other SDK virtual calls abort rather than acknowledge a missing engine backend.

Tests cover two-player jump isolation; pre-spread versus input versus post-movement phases; held jump, exact deadline and expiration; each actual native Jump gate; flag collisions; mode rejection; stale entity serial/private data/map/life; death; and callback revocation without publishing a partial frame. The native Jump gate block is extracted from the current SDK source. This checks the insertion contract, not an installed production hook or complete PM physics.

Production integration order is `BeginPreThink` → guest spread update → `BeginInput` → actual native Jump accepted branch → `AfterMovement` → weapon input → `EndPostThink`. `AcceptedNativeJump` belongs inside the actual native accepted branch, after all real gates. A hook invoked after an early-returned Jump cannot supply acceptance. `Read` is the Glock owner-frame backend; actual FirstDraw/LightState effects update the persistent state through the typed setters.

The test does not claim whole-DLL registration, server gameplay, original CoF movement, audiovisual fidelity or live performance acceptance. Publication excludes original binaries, disassembly, assets and generated test outputs.
