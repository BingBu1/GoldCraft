# Local shared-memory transport

Protocol 27 can move large Minecraft rendering payloads through a Windows pagefile-backed mapping. Only the authenticated local Minecraft-client/CS-client pair negotiates this path. Input, actions, connection ordering and both dedicated-server links retain TCP. Existing world, life, texture-generation and payload validation still apply.

This removes bulk socket transfers and the Java TCP frame concatenation copy. It is not zero-copy: Java copies its owned payload into the ring, and CS copies that ring reservation into an owned message before reclaiming it. Entity extraction, HUD encoding, GPU readback, parsing and OpenGL uploads are separate costs.

## Negotiation and lifetime

The authenticated HELLO capability bit `1` permits a host offer. Internal messages 67/68/69 are offer, acknowledgement and reference. A reference is sent only after the complete acknowledgement is written. These internal messages never reach gameplay consumers. The mapping name is random per connection, its handles are non-inheritable, and a protected DACL grants access only to the current Windows user and SYSTEM. This isolates instances; it is not a boundary against malicious processes running as that same user.

Creation/open failure before activation falls back to TCP. Corruption after activation disconnects rather than guessing which frames to replay. Disconnect destroys each side's view/handle; reconnect creates a new mapping and epoch. No disk file or custom JNI DLL is created. The Java Windows adapter lazily uses the Minecraft installation's JNA 5.14.0; server and non-Windows paths do not initialize Kernel32.

Fallback requires two matching protocol-27 endpoints. It does not make protocol 27 compatible with protocol 26. Deploy the native client/server and Minecraft client/server versions together. Ordinary CS servers remain outside GoldCraft negotiation.

## Bounds and ordering

The ring holds 64 MiB after a 256-byte header. Its aligned 32-bit cursors occupy separate cache lines. Fixed-width fields and acquire/release publication support the x86 CS process and x64 JVM. Unsigned cursor subtraction handles 32-bit wrap; reservations are aligned to eight bytes. No cross-process pointers or GL object identifiers are transmitted.

Eligible payloads of at least 64 KiB use the ring: atlas/patches, section meshes, entity/particle textures, HUD frames, particle meshes, post-effect plans, texture chunks and atomic entity frames. The maximum payload stays 32 MiB. The receiver verifies the published length, original type, cursor and generation before copying; reclamation follows completion of the owned copy.

Full-ring backpressure preserves pending reliable data. Only pose, camera and heartbeat may pass it; relative ordering of other messages is preserved. The client send queue reserves 4 KiB/eight entries for urgent traffic inside its existing limit. Server queue limits are unchanged. Native receive work expands at most 4 MiB per poll, except for one indivisible larger message; a full application queue defers consumption. Existing latest-frame coalescing remains in effect.

For a controlled comparison, set `GOLDCRAFT_SHARED_MEMORY=0` in the processes' launch environment. This disables negotiation and retains the optimized TCP path. It is distinct from comparing against the historical TCP implementation.

## Diagnostics and reproduction

Optional sandbox status files include per-type TCP/shared payload bytes and frames, current shared activation, queue/ring high-water marks, rejections, replacement counts, message ages and aggregate copy/socket work. Mapping names, pairing keys and payload contents are excluded. These measurements do not establish game FPS; live comparisons require the same observer position/orientation and workload.

Client control diagnostics retain 32 transitions and HUD-failure events. Control `blocked` bits are native form 1, life mismatch 2, producer not ready 4, link down 8, pose age >=350 ms 16. `hudBlocked` bits are link 1, presentation eligibility 2, texture 4, revision 8, life 16, cvar 32 and age >=500 ms 64. `uiFlags` indicate GameUI 1, MC menu 2, active UI 4, console 8, active input 16 and native keyboard capture 32. No timeout is relaxed.

The server's 32-event history distinguishes acquire, authoritative-not-ready, death, form change, relocation, map deactivation, disconnect, authority generation/link loss, stale pose and spawn, followed by form requests from AMXX, native commands and initial pairing (cause values 1–14). Form records include the actual requested value and return code, captured before native callbacks can reenter.

Java readiness bits distinguish link 1, geometry 2, actor 4, death 8, dimension 16, form 32, life 64, host death 128, spectator 256, world 512, stale input 1024, missing server lease 2048 and changed identity 4096. One snapshot determines both diagnostics and the unchanged readiness rule. Lifecycle clears are counted separately.

Run `tools/Build-Native.ps1 -Server`, `tools/Build-NeoForge.ps1`, and `tools/Test-SharedMemory.ps1`. The latter uses a separate native x86 receiver and Java x64 producer, checks actual payload CRCs, and supports paced, reliable-paced, full-ring burst and >4 GiB rollover workloads. Tests and transfer benchmarks are separate from actual game rendering/control acceptance.

## Verified checkpoint — 2026-10-11

The combined build passed 163 Java tests in 37 suites, 43 native CTests, 166 native bridge checks and 44 Java/native interoperability checks. The production full-ring test verified six payloads totaling 67,174,536 bytes, including urgent bypass and reliable ordering. Isolated tests additionally verified a real 4,362,076,160-byte cursor wrap, two parallel sessions, reconnects and process termination cleanup.

Three controlled benchmark rounds rotated historical TCP, current TCP and shared memory. Each verified the same 660 payloads / 415,255,080 bytes after warmup; the native receiver used the same logical CPU and measured each stage separately. Against current TCP, shared memory reduced median polling CPU cycles by 55.95% and total measured thread cycles, including the unchanged CRC work, by 16.29%. This is a transport result, not a game FPS measurement.

The real NeoForge B runtime successfully negotiated shared memory with no fallback. A passive 44-second window contained 42 valid samples and four concurrent status-file read failures. Shared delivery increased by 4,487,196,028 bytes; both sides reported zero rejections, all valid samples retained Minecraft control, native GL errors and HUD draw fallbacks stayed zero, and post-effect draws increased by 1,967. Both server and client world identities matched. Two client pose-timeout transitions occurred during initial loading; neither that counter nor server form/control events increased during the observation window.

This establishes actual FML/JNA activation and bulk delivery. It does not establish fixed-view frame-time improvement or eliminate every intermittent menu/form problem. Continue to correlate the bounded histories when those symptoms recur. Independent reproduction also found undefined vertices in the installed NightFall blade producer; its producer-specific repair is separate from transport and is still pending.
