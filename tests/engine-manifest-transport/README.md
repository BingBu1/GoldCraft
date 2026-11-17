# Native manifest transport regression

This fixture executes the production manifest sender and resource writer, the actual ReHLDS bit writers and consistency writer, real hook registries, and the native BZ2/normal-fragment queue. It compares the current sender with the complete previous sender pinned to a Git revision. No game assets or source-tree snapshots are required by the fixture itself; the matching local SDK and compiler dependencies are required.

Run from Git Bash in the configured workspace:

```sh
python -X utf8 -B tests/engine-manifest-transport/run.py \
  --out build/manifest-transport-sdk-local \
  --compiler "$GOLDCRAFT_LLVM_ROOT/bin/clang-cl.exe" \
  --environment build/csol-season2-trace-20261109/compiler-env.json
```

The output directory must be fresh. Set `GOLDCRAFT_LLVM_ROOT` to the matching local compiler installation. The environment file is private generated compiler state and is not published. The runner records the selected source ranges, previous Git revision, compiler/library identities and source hashes; it rejects changed inputs. Only test adapters, objects, packets and receipts are generated under `build`.

The fixture checks v3/v4 and empty/boundary/large manifests, maximum names, custom MD5 and reserved fields, full file size and SHA, consistency indices and both public hook registries. The concatenated uncompressed message bytes must match the old sender exactly. With either hook registry populated, individual reliable group boundaries must also match. Native allocations are tracked through queue draining and must all be released.

For the actual client consumer, build the existing `goldcraft_precache_client_tests` CMake target, then run that executable with `--transport` and the generated `wire` directory. This executes production `parse_resources` over consecutive messages, compares its final records against the server fixture's original source records and verifies final-only download admission. Run it without arguments as well to retain the existing parser/reset regression.

These are component tests. They do not establish live UDP timing, public downloads, GUI progress, map-change performance or multiplayer acceptance. See [the implementation report](../../docs/2026-11-17_优化-资源清单原生分片合批-report.md).
