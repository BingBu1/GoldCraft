# 挑战阶段实际规则验证

sdk.cpp使用实际地图/Session、安装的GameRules、RestartRound和ScenarioThink，再读取受当前runtime保护的阶段。模型、追踪和媒体传输沿用明确的SDK夹具，不是原版客户端画面或多人验收。

先取得当前完整服务端的稳定link-inputs.json，然后在Git Bash运行：

```bash
python -B tests/csol-emergency-kick/run.py \
  --seal build/csol-challenge-phase-full-20261113-02/server/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --sealed-production --fixture tests/csol-challenge-phase/sdk.cpp \
  --groups enabled disabled failed replay \
  --output build/challenge-phase-sdk-new
```

每组使用新的游戏数据目录。若完整对象或源输入漂移，必须重建/重新验证，不能把受拒绝的旧seal重标为成功。图标、BGM、环境及完整奔跑另行验证。
