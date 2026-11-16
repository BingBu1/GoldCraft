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

2026-11-16即时控制回归增加 `live-toggle`、`live-expiry`、`live-invalid`、
`live-missing`、`live-missing-music`、`live-cancel` 和 `live-menu`。前六组通过真实CVAR注册数据和
GameRules::Think检查当前局开关、调时、非法值、缺少环境服务与消息回调撤销；
最后一组执行M菜单桥接使用的真实 `IGameplaySettingsService`、实际后台原子
JSON保存、主线程发布及随后Think消费，检查无需重开地图、关卡或人物。
引擎媒体和玩家身份仍是明确的SDK边界，不代表AMXX权限/画面或多人验收。
环境测试需将 `GOLDCRAFT_TEST_LIGHT_PROVIDER` 指向经身份核验的实际x86测试
lightstyle提供方；没有该提供方时仅 `live-missing` 预期可以拒绝开启。

最终 `build/time-cycle-live-phase-sdk-20261116-03/receipt.json` 使用完整
full02链接封存，二十组206项行为／29,584项SDK检查全部通过，输入稳定。
音乐可选冷准备和真实菜单保存都在该最终组中执行；未部署或做游戏内验收。
