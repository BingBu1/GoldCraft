# Character emotion voice SDK regression

`client.cpp` includes the actual `emotion_voice.cpp` translation unit and uses the project's MetaHook SDK types. Playback, the current view character and resource reads are explicit fixture boundaries. This is not a full DLL or live audio test.

`run.py` accepts an existing frozen client SDK proposal containing the compiler arguments, environment and dependency identities. It freezes production headers and the event consumer, compiles the standalone consumer and the actual consumer fixture, links and runs it, and records exact includes, input hashes and logs. It never deploys or starts a game.

```powershell
python tests/csol-character-voice-sdk/run.py `
  --baseline <client-sdk-proposal.json> `
  --output <new-evidence-directory> `
  --sentences <common/sound/sentences.json> `
  --oracle <verified-original-voice-mapping.json>
```

The oracle contains `characterVoiceOriginal` and `fallbackOriginal` records produced by executing the original bounded mapping functions. Expected mappings do not come from the candidate implementation. The event tests cover voice changes, fallback only when an entry is absent or empty, exact random candidate selection and advancement, template file existence, unknown sentence silence, invalid input and resource exceptions, level changes and same-map reset.

The accepted 2026-11-03 run is `build/csol-character-emotion-20261103/client-sdk-02`: 1,350 checks and no failures, two complete translation units plus link, unchanged inputs and no unknown includes. `client-sdk-01` preserves the initial fixture callback const-signature compile error; its production TU compiled successfully. No runtime claim follows from these tests.
