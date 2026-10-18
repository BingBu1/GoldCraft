# Upstream origin and publication boundary

The human supplied `AMXX-Floating-Damage-1.0.6.zip`, SHA-256
`38def489fb7809f66bfa197225b495990b40db10292a230ac3896eb29716c2cb`.
Main author: Yoshioka Haruki. The original README credits Nexon Studio, Reega!,
Ruby and shel. Their notices and original resources are preserved in the user's
delivery ZIP. No explicit license file or source license grant was found in the
provided archive; do not infer permission to republish the complete source/model.

The public repository contains local additions and a patch to the main source.
The copied main source, fallback include, language file and original README are
locally ignored. The model and compiled plugin are only in excluded delivery data.
The local `non_reapi_support.inc` is archived unchanged and not compiled.

## Verified ZIP members

| Member | Bytes | SHA-256 |
| --- | ---: | --- |
| `source/addon_floating_damage.sma` | 17644 | `838bc268a12a81fdd5de990e6e8cc4a7bd893ba935f7fbaa10778407616c433f` |
| `source/include/non_reapi_support.inc` | 14848 | `b4ea48f9e4833963763343a34e42f09cac8901125de5443d5dfd0a7dacb4b93d` |
| `extra/models/x_re/floating_damage.mdl` | 543160 | `aaba51dfc9972e705870e0fde82806b4a8a3904d509a4f7c77c004bd2e4c8051` |
| `extra/addons/amxmodx/data/lang/floating_damage.txt` | 610 | `cd314ef214497d08b2c60a7b75dddc5570e4fa1e0fbc26f6f0f7829fe78efaa0` |
| `README.md` | 4065 | `4fb8f5bfe58f336359ad51ae6e351dfdd259d70fbd7ab26f72e2c898b7a5330d` |

## Exact implementation references

- AMXX source `8fc12670ea86470a0bfed4c35db5de73d3a39e8a`; compiler 1.10.0.5487.
  The previous 1.9.0.5303 delivery remains a separate historical artifact.
- ReAPI source `1c448d06e8c1cebaea061d6b81b94e85f6262649`.
  Workspace SDK folder is `reapi-5.29.0.358`; its version include reports 5.29.0.359.
- ReGameDLL_CS source `4a50c42e85fd3778c2b7d24731c7d30c83bbab01`.
- ReHLDS source `550f2d62f13f4ebeb029c1d9d1c212133202611d`.

Reviewed matching declarations for TakeDamage's full five-argument post callback,
integer hook return, Spawn, RestartRound, SetThink, entity creation and entvars.
The offline fixture mirrors ReAPI/Fakemeta by-reference variable argument cells;
it executes the production AMXX bytecode in the pinned, unmodified Pawn VM.
The standalone 1.10 VM harness supplies absent engine logging/plugin-manager
adapters for the optional performance wrapper; it calls the actual amx_Exec
interpreter directly, not the performance wrapper.

ReHLDS `engine/common.cpp` MSG_WriteBits clamps an unsigned 8-bit value to 255.
The original model's four bodygroups have 11 choices and bases 1,11,121,1331:
body 255 selects [2,1,2,0], visually 101. The new per-digit body is only 1..10.
The original model and its skin table are unchanged.

For replay, decode upstream main source as UTF-8 and normalize CRLF to LF before
`git apply --check patches/floating-damage-goldcraft.patch` and `git apply`.
Copy the repository's two local includes alongside it and use `build.ps1`.
