# Original M16 derived frame

Executes all1,825 bytes of original CoF MP10051540 from the identity-pinned local sample. The sequence-duration helper1000F390 and condition query100F5DA0 execute their original instructions. Common1011D560, aim exit, reload and animation transport are explicit controlled boundaries.

The cases cover pending reload ordering, jump entry/exit, sprint entry/loop/exit, mixed float/double deadlines, current owner weaponanim and callbacks that change time, flags, velocity or animation sequence. A stale entry-clock negative must fail. Special1/2 lifecycle, full common frame, deployment and gameplay are outside this fixture.

Run directly in Git Bash from the project root with the private pre-edit evidence present:

~~~bash
python -B tests/cryoffear-m16-frame/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-frame.json \
  --compiler "$(python -B -c 'import json; print(json.load(open(".tools/clang-toolchain.json"))["root"] + "/bin/clang-cl.exe")')" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-m16-frame-local
~~~

Use a fresh output directory. Original binaries and evidence are private inputs and must not be published. The runner records actual compiler dependency hashes before and after the build without copying production sources. Actual SDK integration is exercised separately by tests/cryoffear-m16-weapon.
