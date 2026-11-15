# TMP native sound dispatch

Use tests/cryoffear-p345-native-effects/run.py with --weapon tmp and the private matching original MP sample. Set COF_COMPILER to the local x86 Arkari Clang executable.

~~~bash
python tests/cryoffear-p345-native-effects/run.py --weapon tmp \
  --sample analysis/cof-weapons-20261111/samples/hl.dll \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-tmp-native-sound-validation
~~~

The real SDK fixture replays180 original EmptyPlay/pickup/ammo/sleeve cases, checks typed TMP loop/tail/stop delivery through the already verified automatic sender, and revokes edict/ownership/resource authority at callbacks. Source and dependencies are hashed. Missing resources, invalid events, old per-shot FireSound and retirement cannot report success. The temporary automatic catalog must retain authority over the original TMP resource generation.

Passing is component acceptance, without actual native playback, map/Host/client admission, registry, deployment, gameplay or live performance.
