# Client studio-load regression

Runs production LoadStudio and SequenceLoadScope against the pinned MetaHook SDK. Filesystem IO and engine cache allocation are substituted. validation.hpp counts calls to the real validator without approving malformed bytes. Sprite and installation paths are not exercised.

In Git Bash, first generate the toolchain plan with tests/csol-wall-upgrade/run_current_client.py. Then run:

```sh
python tests/csol-studio-loader/run.py --seed build/sequence-probe-guard-20261108-r2/plan.json --output build/studio-loader-new
python tests/csol-studio-loader/run.py --seed build/sequence-probe-guard-20261108-r2/plan.json --output build/studio-loader-old --baseline-ref 82d9fa564980bb926cab447413c19718ac632726
```

Use fresh output directories. The runner reuses the workspace GLEW library. The baseline compiles the old loader from Git through stdin and requires the exact duplicate-validation failure. No game assets, source snapshots or UI automation are generated. These are not live-game performance measurements.
