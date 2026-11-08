# Character emotion voice SDK regression

The fixture includes the production emotion_voice.cpp with the actual MetaHook SDK. Resource reads, current character and sound playback are explicit test boundaries; this is not live gameplay or an audio test.

Run from the project using Git Bash:

```bash
python tests/csol-character-voice-sdk/run.py --compile-commands <client-build/compile_commands.json> --environment <compiler-environment.json> --output <test-output> --sentences <common/sound/sentences.json> --oracle <verified-original-voice-mapping.json>
```

The runner uses existing compiler flags and edits no production source. It builds directly from native/ and tests/, records input hashes and rejects changes during the run. No source snapshots are created under build.

The original instruction oracle supplies characterVoiceOriginal and fallbackOriginal, independent of the candidate implementation. Checks cover non-5050 events, missing characters/sentences, exact candidates and RNG advancement, explicit voice priority, template file existence, invalid tables, load exceptions, level changes and same-map resource reset.

Lazy-resolution checks compare every voice of every real emotion sentence against the original eager expansion under four file-availability patterns. They also verify that unrelated voices are not probed, repeated misses are cached, a character change resolves its own candidates, and resource reset makes newly available sounds visible. The 2026-11-08 run passed 24,318 checks; evidence is in build/skill-sampling-20261108/voice-sdk. Earlier 2026-11-03 acceptance was 1,350 checks. Neither substitutes for live first-skill profiling.
