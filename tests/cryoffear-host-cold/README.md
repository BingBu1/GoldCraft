# Actual guest-pistol cold startup

`run.py` temporarily installs a reviewed DLL and explicit resource closure in
the unused `cs-dione-probe` server. It leaves the main server and graphical
client running, uses a separate loopback UDP/HTTP pair, starts Scenario with
32 slots, and verifies the actual four registered `ItemInfo` records using the
loaded module's matching PDB. It does not create players or inject gameplay.

Supply `--mp`, `--pdb`, `--client`, `--engine`, `--resources` and a fresh
`--output` beneath `build`. The resource JSON contains a `files` array with
explicit `source`, `destination`, `bytes` and `sha256` fields. Include the full
HUD/menu closure, existing guest weapons and any referenced hand donors;
copying a `_hands.json` without its donor is an incomplete test deployment.

The checker compares compiled client/server menu revisions, verifies the
complete server HUD, reads actual registered IDs 896–899 and their clip
capacities, and downloads all listed public guest assets through the real
built-in HTTP service. Server-only damage configuration must return 404.
Non-guest prerequisite resources are checked by the HUD/menu preflight; their
inclusion alone does not claim that every engine resource was downloaded.

All changed runtime files and configuration are restored after exit or failure.
Replacement uses independent temporary files and atomic renames so existing
hard-linked resources are never overwritten in place. An optional existing
x86 CDB supplied with `--debugger` captures `Sys_Error` and access violations
in the isolated test process. Dumps and generated files remain private build
artifacts.

This is actual cold registration/resource/network evidence. It does not prove
weapon input, visual grip, sounds, multiplayer, long-duration stability or game
FPS. Those remain separate gameplay checks.
