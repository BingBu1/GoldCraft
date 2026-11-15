# TMP native factory fixture

Run the existing parameterized actual SDK factory runner with --weapon tmp. Set COF_COMPILER to the local x86 Arkari Clang executable.

~~~bash
python tests/cryoffear-p345-factory/run.py --weapon tmp \
  --compiler "$COF_COMPILER" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-tmp-factory-validation
~~~

Uses production TMPWeapon and NativeWeaponFactory with actual GetClassPtr, OnCreate, CCSPlayerWeapon and world lifecycle bodies. Private allocation/engine callbacks are controlled. Cases cover typed entity creation, explicit cold preparation, repeat creation preserving inventory, missing consumers, callback retirement/reentry, edict recycling and actual Spawn metadata.

Passing does not install the factory in a running map, register purchase/client admission, establish P grip, or prove gameplay/multiplayer/performance. Use a fresh build output; no source trees are copied.

## MP5 uses the same native construction scenarios

`python tests/cryoffear-p345-factory/run.py --weapon mp5 --compiler <clang-cl.exe> --environment <compiler-env.json> --output <fresh-build-directory>` compiles the distinct MP5 factory and SDK weapon type. The shared scenario body switches only the selected class/factory and explicit metadata. It also rejects a TMP object through the MP5 factory and an MP5 object through the TMP factory. Both guns retain separate cold providers and native wrappers; this does not install either factory in the live map owner.
