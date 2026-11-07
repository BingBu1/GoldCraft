# Animation arena regression

The fixture compares the current packer to append-only revision
`d39014de17dfcf7b665fe955fc5bc13c54be261e`. The old translation unit is read with
`git show` and compiled through stdin with only its two exported function names
renamed. No historical source tree or old source file is saved.

`fixture.cpp` reuses the existing bounded animation sampler, unit conversion
oracle around sequence indices 197/198, and safety tests. Additional checks
compare animation bytes including terminal padding, descriptors, events,
pivots, geometry consumers, scales, safety selection status and idempotence.
Cases include shared/nonmonotonic native animations, insufficient arena capacity,
unknown optional fields and live references overlapping the candidate arena.
The legacy test's whole-prefix byte equality is deliberately excluded because
the candidate legitimately changes dead animation/descriptor storage.

Run `python tests/csol-sequence-arena/run.py --output build/csol-sequence-arena-plan`
to prepare commands without compiling. After receiving the coordinator's single
job window, run with a fresh output directory and `--run`. `--resources single`
(default) adds csol89; `--resources all` checks all 150 available corpus aliases.
The historical manifest labels csol28/csol32 headers unsupported by its old
parser; the current decoder handles both and this runner includes them.
The local corpus manifest and toolchain environment must already exist.
Add `--original` to run the formal `sequence_tests.cpp` entry point, covering
leet/urban resources and the existing zombi_host rejection, in the same binary.

Compilation is serial x86 Clang C++20/O3/ThinLTO/AVX2/precise, with direct project
sources and `/threads:1` linking. Receipts preserve commands, logs, Git revision,
old source blob/hash, current sources, toolchain headers, compiler and read-only
resource identities. Actual linker library opens are checked against library
hashes taken before compilation. This is offline fidelity/safety validation, not deployment,
gameplay or measured live performance.

Resource comparisons use explicit `AllowUnsupportedTracks`: they compare bytes,
metadata and selection refusal even when a track is unsupported. The whole-model
numerical oracle runs only if every track is Ready. In the 150-character corpus,
csol87 has 45 unsupported tracks (indices 308–352), so only the other 149 models
receive that numerical check. The fixture prints each unsupported label and
checks equal old/new endpoint refusal and equal strict rejection; a passing
permissive test never authorizes strict resource generation or gameplay support.
