# Original CoF player Think scheduling

Replay the exact private MP scheduling helper and complete engine PlayerRunThink against the real SDK PlayerThinkSlot. A matched original pair, private pre-edit proof and precision supplement are required. No original bytes are embedded or published.

From Git Bash, use a fresh output directory:

~~~bash
python -B tests/cryoffear-m16-weapon/run.py \
  --sample analysis/cof-weapons-20261111/m16-integration-20261115/cof-mp.dll \
  --proof analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-sdk.json \
  --player-think-engine analysis/cof-weapons-20261111/server-evidence/cof-engine/hw.dll \
  --player-think-proof analysis/cof-weapons-20261111/m16-integration-20261115/engine-scheduler/PREEDIT-player-think.json \
  --compiler "$COF_CLANG_CL" --environment "$COF_COMPILER_ENV" \
  --output build/cof-player-think-local
~~~

Set COF_CLANG_CL to the configured x86 clang-cl and COF_COMPILER_ENV to its saved SDK environment. The runner reuses actual M16 lifecycle composition and compiler dependency sealing; it does not copy production sources. Oracle inputs, expected answers and evidence are hashed before/after. Empty, truncated, extra and wrong answers must fail.

The original CRT initializes 53-bit x87 precision. The oracle explicitly uses CW027F and MXCSR1F80; externally changed FP controls are outside this result. Cases cover near-equality with double clienttimebase, overdue clamping, external deadline clearing before callback, replacement/rescheduling, dormant/kill flags, holster/drop/model changes and native Think preservation. Original target damage and native entity freeing are controlled boundaries.

The slot still requires an exact host call after PlayerPreThink and before PM_Move. StartFrame or PostThink is not equivalent. Compilation/oracle success establishes neither map admission nor live gameplay/performance.

宿主新增SourceHostId仅用于记录已接受任务的发起武器，选择其地图资源和显式JSON伤害；它不是原CoF字段，也不创建第二个任务。新增断言覆盖无效请求保留旧来源、成功替换、换模型/收枪保留与退休清零。原截止时间、调用顺序和调度对照不变。
