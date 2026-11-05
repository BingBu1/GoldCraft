# SKULL3 Deploy against original instructions

`deploy.cpp` invokes the production `native/csol/skull3.hpp` implementation. Expected results come from the private original-MP instruction emulator, not from a mirrored formula in this test. `run_deploy.py` freezes the exact source and records its SHA-256, compiler, original oracle identity and results.

Run `python tests/csol-skull3/run_deploy.py --oracle <original-instruction-oracle.json> --output <new-build-directory> --environment <verified-x86-environment.json> --compiler <clang-cl.exe>` from the repository root. The environment input can be a JSON environment dictionary or a build proposal containing an `environment` dictionary. The test uses x86 C++20/O3/ThinLTO/AVX2/precise.

The original oracle covers single/dual, normal/conversion deployments, empty/partial/full clips and reserves, and dual left-shot-bit preservation. Its scope is the original Deploy tail with explicitly recorded getter/interface spies. It does not establish complete acquisition, prediction, live animation or gameplay acceptance. Original binaries, decoded instructions and oracle traces stay in excluded local analysis/build storage.

The title consumers use a completed client build as their SDK and compiler baseline. Pass its directory and the matching private TITLE-08 verification receipt explicitly:

```powershell
python tests/csol-skull3/client_titles.py --baseline build/skull3-audit-20261105/client-full-02 --evidence build/skull3-audit-20261105/title08/verification.json --output build/skull3-title-recheck
```

`--baseline` supplies `environment.json`, the effective source manifest, build commands and the completed build receipt. `--evidence` must identify the verified original client SHA and contain zero failures. The test plan records the evidence and environment SHA-256 and rechecks them after execution. There is no dependency on the resource collector's private build directory. The output directory must be new; the private original evidence is supplied by the developer and is not published with the source.
