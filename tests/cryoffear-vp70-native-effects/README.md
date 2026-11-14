# VP70 native sound consumers

The shared runner accepts --weapon vp70 and the exact original x86 MP sample. Its instruction oracle covers192 cases through the original empty, sleeve, pickup, ammunition and native FireSound consumers. Shooting uses CHAN_WEAPON, volume1, attenuation0.8, pitch100, preserving original quiet-audio handling. It never routes native shots through the local PlaySFX implementation.

The actual VP70 SDK adapter is compared at the engine EmitSound edge. Additional mutation cases revoke actor/item/resource identity at each callback boundary, including the sound callback itself. Original files and instruction captures remain private; compiler/tests and evidence mapping may be published. Device output, transport, real gameplay and performance are separate acceptance boundaries.

Set COF_COMPILER to the configured x86 clang-cl executable before running.

~~~bash
python tests/cryoffear-p345-native-effects/run.py --weapon vp70 --sample analysis/cof-weapons-20261111/revolver-integration-20261113/full-mp/hl.dll --compiler "$COF_COMPILER" --environment build/csol-season2-trace-20261109/compiler-env.json --output build/cof-vp70-native-audio-new
~~~
