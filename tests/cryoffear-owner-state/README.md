# Original owner-state operation checks

The runner compiles the new pure button-edge and jump-state operations as x86 C++20 with O3, ThinLTO, AVX2 and precise floating point. It compares their results with execution of the exact original server module's unmodified x86 instructions. The private sample must have SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`; it is not distributed.

Supply `--compiler`, the existing toolchain `--environment` JSON, `--original-sample` and a new workspace `build` directory through `--output` to `run.py`. The output records the Git revision and before/after source hashes without source snapshots.

Cases cover held/released/repressed button sequences, jump gates including water-type exceptions, exact float32 deadlines, noncanonical state bytes, equality, infinities and NaNs. The state has no default/reset policy. Calling the accepted-jump marker requires an actual accepted jump, and the adapter must preserve the original PreThink/movement/PostThink order.

This validates only the four pure operations. It does not validate construction, spawn/death/save/restore, sprint inputs, full Jump animation/physics, server integration, gameplay or performance. The caller must supply authenticated owner state; absent sprint or lifecycle information must not be filled with false/zero.
