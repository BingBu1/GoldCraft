# Audit authored hand joints

The production audit reads actual MDL triangle connections and samples every original sequence, blend and frame. It detects changed target payload, rig, attachments, topology, UV indices, timing and animation channels. It reports cross-bone edge lengths relative to the donor bind geometry, including the exact worst frames and vertices. It does not assign a guessed visual pass threshold or prove hand contact with a grip.

Run directly in Git Bash from the repository root with Python 3.13 or newer:

```bash
python tools/Inspect-CryOfFearHandJoints.py \
  --target path/to/original-v-model.mdl \
  --donor path/to/paired-cso-hands.mdl \
  --candidate path/to/authored-hands.mdl \
  --output build/hand-joints-review/report.json

python tests/cryoffear-hand-joints/run.py \
  --model path/to/paired-cso-hands.mdl \
  --output build/hand-joints-tests
```

Output paths must be new, inside `build`, and free of junctions or symbolic links. Input game resources are supplied locally and are not distributed here. The test uses the actual author and parser, injects a large connected-bone displacement, and verifies rejection of changed source payload, sequence timing, animation channels and triangle topology. Game resources remain unchanged.

The audit retains only the current pose, current model vertices and eight worst measurements per hand model. Reported scale ratios are geometric measurements: different source units and intended animation deformation still require review. This tool adds no game draw or input work. Offline inspection and the OpenGL adapter test do not replace actual gameplay or performance acceptance.
