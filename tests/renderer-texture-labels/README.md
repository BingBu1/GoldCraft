# Renderer texture debug labels

Run from the project root through Git Bash with the pinned x86 Clang toolchain
and captured MSVC SDK environment:

```bash
python -X utf8 -B tests/renderer-texture-labels/run.py \
  --compiler "$GC_CLANG_DIR/clang-cl.exe" \
  --environment "$GC_COMPILER_ENV" \
  --output build/renderer-texture-labels-new
```

Use a fresh output directory. `GC_CLANG_DIR` and `GC_COMPILER_ENV` refer to the
existing project toolchain and SDK environment; the runner creates no source
checkout. It extracts the two exact production functions into a generated test
adapter, includes the actual SDK CVAR declaration and checks the real default
registration. Compilation uses C++20, AVX2 and precise floating point with
contraction disabled.

The executable creates an invisible Windows OpenGL context. It verifies missing
CVAR registration, disabled labels, enabled literal/formatted names, the existing
255-byte truncation, an absent driver entry point and runtime disabling. Disabled
paths must perform no formatting or driver label calls. Enabled paths use the
real `glObjectLabel` and `glGetObjectLabel`. Pixel readback and texture binding
must remain unchanged.

The same executable uploads and generates mipmaps for identical bounded texture
batches with labels off/on, reporting queue and completion time separately.
These are diagnostic GL measurements, not game admission, FPS, wall-clock
speedup or multiplayer evidence. `r_debug_texture_names` defaults to 0; set it to
1 to label subsequent uploads for debugging. Changing it does not rename every
existing texture or initiate a resource reload.

`--original-labels` extracts the pinned upstream functions instead. Compilation
and linkage must succeed, then the real GL regression fails because the old
implementation formats and labels textures while disabled. Keep its failing
receipt separate from the corrected run.
