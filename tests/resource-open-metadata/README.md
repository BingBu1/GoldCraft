# Client file-open metadata

Compile the production overlay against the actual reviewed x86 IFileSystem009, then compare it with the same DLL's unscoped native Open. The fixture checks all eight partial-hook failures, loose files, PAK interleaving, path IDs, writes, overlay changes, missing files, nested opens, another caller's stat, overlapping thread scopes and exceptional close/unwind. A counter on the original virtual stat proves that the path query still runs for the negative control and unrelated callers. A separate native CRT import hook verifies that a later import replacement retains its original callback chain.

Run commands directly in Git Bash from the project root. These paths reuse the existing compiler setup; supply a current equivalent if its build directory has been cleaned.

```bash
python -X utf8 -B tests/cryoffear-download-contract/run.py \
  --commands build/resource-bridge-20261108/client2/compile_commands.json \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --filesystem analysis/resource-crash-20261108/FileSystem_Stdio.dll \
  --output build/client-open-metadata-component-20261117-01

python -X utf8 -B tests/resource-open-metadata/run.py \
  --component build/client-open-metadata-component-20261117-01 \
  --filesystem analysis/resource-crash-20261108/FileSystem_Stdio.dll \
  --resources build/fs-metadata-experiment-20261117-01/resources.txt \
  --output build/client-open-metadata-corpus-20261117-01
```

The resource list contains one absolute workspace sandbox file per line. The native fixture mounts their shared cstrike root under a dedicated path ID; it only reads the real resources. It compares size, both handle timestamps and initial bytes against independent file reads before alternating scoped/native/scoped runs. Source and resource identities must remain unchanged. Generated files stay under the fresh build output.

This proves actual file-system semantics and component cost. It does not establish whole-map loading performance, original CSOL performance, network download behavior, GPU timing or multiplayer acceptance. DLLs, resources and the private resource list are not published.
