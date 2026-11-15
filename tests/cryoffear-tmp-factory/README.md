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
