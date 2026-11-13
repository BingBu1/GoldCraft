# P345 native sound consumers

This compiles the actual P345 class and native engine sound backend. The private
original module must match SHA-256
`0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`.
The original P345 virtual table +0x190 selects shared EmptyPlay; the backend uses
its two actual empty-state resources, sleeve selection, pickup and ammo pickup.
FireSound remains the separate PlaySFX path.

```bash
python tests/cryoffear-p345-native-effects/run.py \
  --sample "$COF_ORIGINAL_DLL" --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-p345-native-sound-check
```

The suite compares 180 original consumer cases, actual native resource/emitter/
channel/volume/attenuation/pitch/flag arguments and 56 callback retirement cases.
Five malformed oracle inputs must reject. Engine callbacks are controlled edges;
this does not prove audible gameplay, networking, deployment or live performance.

All resources are cold-prepared immutable paths. No path rewriting, resource
lookup, allocation, file access or RNG is added to native sound dispatch.
