# M16 original artwork and host HUD integration

Run from Git Bash with private original resources and an isolated five-gun,
version-16 presentation baseline:

```bash
python -B tests/cryoffear-m16-presentation/run.py --original ORIGINAL_COF_ROOT --baseline BASELINE --output build/m16-art-NEW
python -B tests/cryoffear-m16-presentation/run_hud.py --environment ENV_JSON --commands CLIENT_COMPILE_COMMANDS --output build/m16-hud-NEW
```

The author first prepares MP5, then M16. It pins the original inventory script,
picture and two-frame magazine sprite; preserves six previous catalog rows;
compares both frame pixels and native sprite bytes; checks native selection
conversion and the real download preflight's resource list. Generated art is
private runtime data and is not published.

The HUD runner compiles project sources directly, including the complete
existing HUD parser/loader/draw fixture and font registration owner, with the
HUD target's SDK include flags. It checks partial/malformed mode resources,
recovery, exact two-frame ownership, mode selection without new resource work,
and absence of a guessed icon. VGUI, engine services and images are controlled;
this does not validate original game pixels, audio or performance.

The actual dedicated message/FOV lifecycle, per-gun admission, resource audio
owner and seven real P-model muzzle routes have their own existing suites.
M16's production owner/target backend remains a separate gate; these tests do
not open purchases or authorize deployment.
