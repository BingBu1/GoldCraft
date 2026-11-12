# CoF core rendering adapter

Builds the production decal and point-light backends against the actual x86
client SDK, then creates real OpenGL compatibility and 4.4 Core contexts on the
same GPU. It compares static and transformed-brush lighting in both original
attenuation paths and multiplicative shot decals, with a one-byte RGB tolerance.
Depth and GL errors are checked separately. These are hidden GPU fixtures, not
GoldSrc gameplay or a performance benchmark.

```bash
python tests/cryoffear-core-render/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-core-render-check
```

Keep the existing original-instruction and compatibility tests in
`cryoffear-point-light-render` and `cryoffear-custom-decal`. The adapter preserves
their geometry, texture builders, gamma math and ordering. Matrix authority comes
from the actual renderer frame; a Core draw without authenticated matrices is
refused. Map geometry uploads happen during cold preparation, and changed decal
geometry uploads before drawing. Draw does not load files or allocate geometry.
