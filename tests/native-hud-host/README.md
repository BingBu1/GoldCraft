# Native HUD Host binding regression

This fixture executes the production `resource_native_hud_host.cpp` and its image validators against the matching x86 SDK. It covers API, module CRC, image bounds, instruction and symbol rejection; atomic binding publication; delayed and repeated Surface capture; dynamic screen queries; trampoline results; and exact array, source and texture release owners.

Run from the repository using explicit workspace copies of the reviewed original DLLs:

```powershell
python tests/native-hud-host/run.py --output build/native-hud-host-next --compiler <clang-cl.exe> --sdk <workspace-MetaHookSv-root> --client <workspace-original-client.dll> --engine <workspace-original-hw.dll> --environment <toolchain-environment.json>
```

The output directory must be new and inside the workspace. `--environment` is optional when the calling environment already contains the Windows toolchain. No private paths are built into the test. Both module copies must be within the workspace; the runner verifies their complete expected SHA-256 before mapping sections. The executable applies the original PE relocation records at its actual allocation base.

Only the original six-byte Surface getter and the original allocator jump chains execute from copied image memory. Their final imported allocator targets point to distinct test recorders. The native HUD export itself never runs: its required hook trampoline is a fixture callback. The Surface provider derives from the real SDK interface. These boundaries test binding and routing without launching a game or relying on another guessed HUD implementation.

The runner performs serial syntax scans to discover SDK headers, freezes the workspace source/header closure, then compiles three translation units and links with one job. Toolchain headers, compiler, original images, mapped inputs, actual include paths, objects, executable and logs receive SHA-256 receipts in `result.json`. Existing output is never overwritten.

This is a binding regression only. It does not exercise lifecycle event generation, Host_Error recovery, resource downloads, plugin integration, v4 capability, a complete DLL, rendering or gameplay. The existing 1666-check HUD transaction matrix is not repeated.
