# Revolver delayed shell sound event

`python tests/cryoffear-revolver-events/run.py --sample "$COF_CLIENT_SAMPLE" --environment "$COF_COMPILER_ENV" --output build/cof-revolver-event-validation`

Compiles the actual client SDK consumer and replays the complete original client10052750 handler. The fixture deliberately gives entity origin, incoming event origin and attachment0 different coordinates. It checks exact lookup/RNG/EV_PlaySound ordering and parameters, missing entities, NaN/raw coordinate bits, cancellation and missing backends. It verifies the actual SDK offsets rather than applying original offsets to production memory. No model ejection, event hook, network delivery or actual mixer/gameplay/performance acceptance is inferred.
# Production native event bridge

Pass `--commands <actual-client-compile_commands.json>` to `run.py` to include the production event registration, native signon query and connection/resource lifecycle adapter. The actual SDK test covers ordinary servers with no resource, native audio without local PCM, remote entities, cold-only inspection, thread ownership, reentrant retirement and shutdown/reinitialization. Run with the copied original client sample and the matched compiler environment; use a fresh workspace `build` output.

This extends the original complete-handler differential test. It does not claim real event networking, whole-weapon admission, rendered/audio gameplay or performance acceptance.
