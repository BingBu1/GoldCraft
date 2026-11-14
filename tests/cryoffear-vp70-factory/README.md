# VP70 native factory

Run the shared actual-SDK factory runner with --weapon vp70, an x86 clang-cl compiler, the workspace compiler environment and a fresh build output. This fixture calls the production NativeWeaponFactory, GetClassPtr, OnCreate, CCSPlayerWeapon wrapper and VP70 Precache/Spawn. It verifies host identity899, scenario guest metadata, repeat creation, recycled edicts, revoked map leases and reentrant retirement. Engine allocation and event consumers are controlled boundaries; this is not shop or gameplay acceptance.

Set COF_COMPILER to the configured x86 clang-cl executable before running.

~~~bash
python tests/cryoffear-p345-factory/run.py --weapon vp70 --compiler "$COF_COMPILER" --environment build/csol-season2-trace-20261109/compiler-env.json --output build/cof-vp70-factory-new
~~~
