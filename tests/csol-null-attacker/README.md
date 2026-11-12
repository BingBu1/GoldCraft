# NULL attacker regression

The SDK test calls the actual current player TakeDamage, armor, base health, Killed, rules PlayerKilled/DeathNotice/SendDeathMessage and native hookchains. It covers absent/non-null damage, a pre hook changing a player attacker to NULL, victim retirement in the pre hook, no surrogate allocation, no absent-killer frag/reward writes, real win checks, actual player kill credit and actual world non-null frag behavior. DeathMsg byte0 remains distinct from the NULL hook pointer. The unchanged active Ham HLTypeConversion helper also runs with NULL=-1 versus real world0.

The separate ReAPI test compiles the actual hook_callback.cpp, amx_hook.cpp and reapi_utils.cpp in place. Its five wrappers and real forwarding templates send NULL as -1 to a controlled VM ExecuteForward boundary; pre hooks can set -1 and the actual PEV/getPrivate path returns NULL to the controlled next-chain. This is not an AMXX VM or full server integration test. Unused module interfaces are NULL-bound and unexpected AMXX errors throw.

Use exact Git Bash from the project root:

~~~bash
python -X utf8 -B tests/csol-null-attacker/run_sdk.py --output build/null-attacker-sdk-new
python -X utf8 -B tests/csol-null-attacker/run_reapi.py build/null-attacker-reapi-new --compiler "$CSOL_CLANG_CL"
~~~

The SDK runner accepts --reuse pointing at a prior targeted receipt: it reuses matching source/object identities and recompiles changed TUs. It pins the approved full SDK link seal and verifies every reused object. It does not build a DLL, start Scenario, or instantiate its changed Models/Session layout against old support objects. The parent performs the complete latest build.

Generated build adapters only bridge Ham's missing sdk_util include to the actual SDK util declarations and ReAPI's absent generated version include to its exact version.h. No production body is copied. The Ham header's legacy EHANDLE declaration is isolated in the test namespace. Engine services, studio/animation and message transport are explicit fixtures. No runtime deployment or live gameplay is performed.
