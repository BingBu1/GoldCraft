# Cry of Fear point-light backend checks

The production component is [point_light_render.cpp](../../native/guest/cof/client/point_light_render.cpp). It implements the original custom point-light path, not engine dynamic lights. The fixture uses the actual MetaHook SDK and a hidden native WGL compatibility context.

Run from Git Bash with a matching Visual C++ environment JSON (INCLUDE, LIB, LIBPATH and PATH), the configured Clang toolchain, Python with Unicorn, and a private original client module:

~~~sh
python tests/cryoffear-point-light-render/run.py --environment "$COF_BUILD_ENV" --sample "$COF_ORIGINAL_CLIENT" --output build/cof-point-light-check
~~~

The accepted original client SHA-256 is `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`. No original module, instruction bytes, assets or build environment are distributed.

The oracle executes original x86 instructions for all attenuation texels, the normalization cube, 2,048 tangent matrices, 6,144 texgen planes and 1,024 gamma cases. The gamma exponent-one branch retains x87 intermediate precision. The GL probe exercises actual three-dimensional attenuation, texture-environment multipass and the NV register-combiner branch when supported, including state restoration, visibility mismatch, backface and bounds rejection, capacity refusal, static brushes, and the real 92/108-byte surface layouts. Receipts distinguish compilation, numeric comparison and actual GL execution.

Integration remains gated. This component requires an owner-thread compatibility context with at least four fixed-function texture units and a genuine original-order gamma scene after opaque geometry and before transparency. It requires matching depth/projection/range and correctly produced independent node/surface visibility stamps. It uses the original client-array path and refuses a nonzero current VAO. Static zero-origin/zero-angle brush entities are supported; transformed and portal brush entities use a separate original light stage and refuse readiness here. Spotlights are unsupported. These gates must be supplied by actual host evidence; setting flags or allocating a light pool is not integration.

Cold map preparation copies immutable vertices/bases and preallocates bounded draw storage. Cold GL initialization creates owned textures. Draw performs no application heap allocations, uploads, file reads or network work. Compatibility state snapshots and NV extension queries have costs that still require real game profiling. The alpha fallback intentionally uses framebuffer alpha as the original scratch channel; it preserves depth/stencil. Original-module numerical agreement and fixture pixels do not establish gameplay fidelity, host lighting/color-space equivalence, multiplayer behavior or performance acceptance.


The moving-brush extension accepts actual same-frame opaque submission matrices and resolved angle bases. Original1005ADA0 subtraction and ordered dots match 1,536 scenarios bitwise; the hidden GL suite now has 105 checks including translated/rotated depth replay, camera/depth/stencil restoration and backface/lifecycle refusal. Legacy entity/stamp callers retain strict transformed-brush rejection; captured normal brushes use the independent dynamic stage after world/static-light passes. Portal effects70/137/184 and spots remain unsupported. No gameplay or live performance acceptance is implied.
