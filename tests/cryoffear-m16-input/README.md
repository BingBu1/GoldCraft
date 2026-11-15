# Original M16 common-frame input

Runs all2,055 bytes of original1011D560 under an explicit M16 input-stage boundary: no loop sound is active, wear and reserve HUD are stable, and any reload is not due. M16 GetItemInfo10050E40 executes to populate the real original item metadata table. CanAttack1011BFC0 and ladder100E4ED0 execute their original instructions.

The1,828 cases cover combined buttons, secondary/aim/primary/reload precedence, absolute clock equality, activity blocking, ladders, pending reloads, legitimate large magazines, and pre-input or inventory consumers changing time/buttons/movement. An entry-clock snapshot negative must fail. Common sound lifecycle, full common-state equivalence, deployment and gameplay are not asserted.

Run in Git Bash from the repository root, with the private sample and pre-edit archives available. Use a fresh output directory:

~~~bash
python -B tests/cryoffear-m16-input/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-common-input.json \
  --compiler "$(python -B -c 'import json; print(json.load(open(".tools/clang-toolchain.json"))["root"] + "/bin/clang-cl.exe")')" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-m16-input-local
~~~

Compiler dependencies are discovered and hashed before/after the build; production sources are not copied. Actual SDK composition is exercised separately by tests/cryoffear-m16-weapon. The initial missing-metadata fixture failure and subsequent fixture-initialization failure are retained in local historical outputs; neither is a production regression result.
