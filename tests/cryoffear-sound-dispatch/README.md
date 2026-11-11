# CoF server sound and effect dispatch regression

This suite compiles the production senders against the actual ReGameDLL SDK and compares their requests with the complete original x86 sound sender, recipient helper, substring-search and shell instructions, plus the cooperative recoil branch. The private sample must match the SHA-256 checked by the oracle. Original binaries and extracted instructions are not published.

Run from the repository root in Git Bash, supplying the local x86 Clang toolchain and compiler environment:

```bash
python tests/cryoffear-sound-dispatch/run.py \
  --sample "$COF_SERVER_SAMPLE" \
  --compiler "$CLANG_CL" \
  --environment "$COMPILER_ENVIRONMENT" \
  --output build/cof-sound-sender-check
```

Use a fresh output directory. The runner records the original identity, compiler-discovered dependencies before and after the build, compiler identity, compile/run results and eight malformed-oracle rejection cases. It generates only test adapters and outputs; it does not copy production sources.

Coverage includes the Direct and Cooperative branches, the exact case-sensitive `weapons` substring, unconditional Cooperative gun-position replacement, local and broadcast routing, actual recipient indices, native coordinate wrapping, 32 recipients, cancellation at SDK boundaries, entity reuse, borrowed-request mutation and recursive sends. A message that has begun always finishes from a stack snapshot before reporting cancellation.

Shell coverage retains native PVS routing, all six coordinate inputs, angle, model, bounce and life. Recoil retains the original owner-only event flag, both impulse vectors and every zero scalar. Smoke uses the existing versioned host codec and local owner mapping. Cross-sender reentry, borrowed-request mutation, malformed requests and cancellation are checked; a started packet always completes.

The authority context and callback must remain valid through the entire call, including packet completion after retirement. The shared recursion guard coordinates these CoF senders only; other engine hooks must not open a nested message. Rule selection, the client/resource owner and real event/message-ID lifecycle remain integration responsibilities.

The CoFSFX/CoFParticle wire formats and ReHLDS coordinate encoding are host adaptations. The oracle controls the rules, entity-instance, gun-position and native message/event edges; this is not a full original engine execution, deployed-weapon test, listening test or game performance measurement.
