# MP5 original presentation

Run from Git Bash with the read-only original CoF game root supplied as `COF_ROOT`:

```bash
python -B tests/cryoffear-mp5-presentation/run.py \
  --original "$COF_ROOT" \
  --baseline sandbox/cs-server/Half-Life/cstrike \
  --output build/cof-mp5-presentation-check
```

The real offline author verifies the original inventory mapping, MP5A3 card and magazine identities. This test checks full native magazine bytes, decoded modern pixels, palette fallback transparency and quantization, unchanged existing catalog/menu records, and six firearms' native HUD closure. Atomic publication exercises temporary sharing conflicts, permanent denial and an existing destination without replacing it.

Consume the generated `mp5` directory with `tests/cryoffear-revolver-hud/run.py --resources` and `tests/cryoffear-host-admission/run.py --presentation`. The actual HUD test covers independent MP5 module registration, original icon texture pixels, complete authoritative clip/reserve numbers, zero allocation or I/O during 100 redraws, and video resource retirement. The generated `native-closure` is the input to `tests/cryoffear-resource-preparation/run.py --presentation`.

This is authored CSO presentation using original CoF artwork. Original client evidence and host adaptations are recorded in [the MP5 HUD report](../../docs/2026-11-15_接入-MP5原图HUD与购买资源-report.md). These suites do not establish production Host installation, complete optional HUD availability, real UI input, actual GPU pixels, gameplay, multiplayer or FPS acceptance. Validate the complete authoritative server HUD closure separately before deployment.
