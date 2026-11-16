# 随身光控制台 CVAR 即时生效

修复永夜随身光只响应 M 菜单保存、不响应直接控制台 CVAR 修改的问题。四个参数现在由当前规则帧直接读取注册变量；不需要换图，控制台临时覆盖不改写 JSON，M 菜单保存仍使用同一个持久文件。双端完整构建、实际GameDLL及隔离真实专服检查已过；当前B和主专服尚未更新。

## 范围与依据

这是用户指定的客户端管理随身光及即时CVAR接口，不修改CSOL原版还原算法或灾变AI。修改前的源码身份和行为结论保存在 `analysis/night-player-light-controls-20261116/preedit.json`。旧`CCSOLChallengeEnvironment::Update`只在保存的不可变JSON指针变化时重读灯光配置；注册CVAR的即时数值被绕过。描述符宣称`Immediate`并不能使消费者自动变成即时。

| CVAR | 当前局行为 |
| --- | --- |
| `csol_custom_night_player_lights` | 0关闭随身光，1按当前有效参数启用；时钟和关卡不重开。 |
| `csol_custom_night_player_light_radius` | 下一规则帧应用，合法范围16～512，支持小数。 |
| `csol_custom_night_player_light_brightness` | 下一规则帧应用，整数0～255；0保留零亮度语义。 |
| `csol_custom_night_player_light_color` | 下一规则帧应用，整数0～16777215，表示RRGGBB；0保留无光语义。 |

非法数值保持上一个有效策略，不发布错误包；即使另一个数值非法，合法的关闭请求仍能关灯。纠正数值后无需重载即恢复。M菜单事务保存发布后，用已保存的完整设置替代临时控制台覆盖；仍按userid／修订号／原子落盘顺序执行。

## Evidence、Finding与Path

| 编号 | 原始检查回执 | SHA-256 |
| --- | --- | --- |
| E-001 | `build/night-light-controls-negative-sdk-20261116-01/receipt.json` | `4e143d993678b52952c4d7c96e31887954efea6fe76c0830fb81892ceb701b8d` |
| E-002 | `build/custom-live-final-server-full-20261116-02/receipt.json` | `883ff0275a8469fc6bc52eecf41cd594f26c6dd90853ec3c9acc7abd63aabe87` |
| E-003 | `build/custom-live-final-client-full-20261116-01/receipt.json` | `f96d1b6a021e810553d8e325c757323a0c8942d5049faa68709df6213969afd1` |
| E-004 | `build/night-light-controls-sdk-20261116-03/receipt.json` | `ab172915d55095661723aa370b1e8ec22c079d54631ecd6a69a8c7df9ef43d31` |
| E-005 | `build/night-light-controls-client-sdk-20261116-01/receipt.json` | `c3213476e20e2c0bf764886a2f2a58aab1de35befc2757b60c95b479ecc720a2` |
| E-006 | `build/night-light-controls-dedicated-20261116-02/receipt.json` | `31749acdace698f723742277f989f837f0127a0794c57c24372662cce7acfed1` |

F-001，`status=validated`、`severity=n/a_re`、`confidence=high`，E-001/E-004：新增`environment-console`在未修复的实际GameDLL对象上得到17项行为失败，编译和链接均已成功；不是用编译错误冒充回归。修复后该用例和其他26组全部通过，共270项行为／41808项SDK检查，输入稳定。覆盖临时开关、数值、非有限／越界／非法小数、禁用优先、50未变化帧零新增包，以及控制台覆盖之后的真实菜单保存。

P-001，`path_type=callflow`，E-001/E-004：引擎注册CVAR数值 → `CSOL_ReadNightLightingControls`验证并构造策略 → 实际规则Think／环境owner比较上次策略 → 仅变化时发布24B `CsoNLight` → 既有客户端灯池本地跟随。新增函数在`native/csol/server/custom/gameplay_settings.cpp/.h`；消费者在`challenge_environment.cpp/.h`。移除旧共享快照缓存成员，不增加服务器玩家遍历、灯实体、坐标包或AI操作。

F-002，`status=validated`、`severity=n/a_re`、`confidence=high`，E-004/E-006：直接控制台修改不重写JSON；实际M服务的授权请求后台原子保存后，下一Think发送保存策略，保持同一规则对象、runtime generation、关卡和环境。真实隔离专服另确认环境准备、当前图时间开关／17-19调时／非法值诊断及重开，共15项检查通过，256项完整HUD闭包、3项HTTP、11项临时文件恢复。该专服无人且round=0，不能冒称战斗阶段的灯光画面或真人权限通过。

## 构建、失败和验证边界

最终完整x86服务端MP为`2a77e49b3bc6c3ccc064e41f9f7f305e26a684b1145670a5c68e2b4b51ddb99b`，完整客户端为`dec7218877dd5707d06d2bb1bced69b2fae3a9c2e53d3343b0b2dbf626317897`，均记录输入稳定。客户端保留自动资源刷新能力，实际依赖597项；环境协议56、原环境客户端41、随身光70、配置953检查通过，32槽×2000移动帧零分配。Renderer此前735输入及成品身份核同；AMXX菜单源与已编译插件也核同。没有以这些CPU夹具代替实际GPU帧时。

联合夹具首轮错误提前给`live-missing`加载引擎，负例2项失败；已令该分支保持无提供方后，最终27组通过。冷服组合检查首轮把JSON路径变量覆盖为环境内存地址，报`int.read_bytes`，已改为独立`environment_config`，第二轮通过。一次回归启动早于link-inputs封存完成而被拒绝，也单独留档。没有忽略这些失败或改玩法来迎合测试。

本次热路径直接读取4个注册数值，固定成本，无字符串查找、文件／网络I/O、分配或逐玩家工作；新增验证不是实机性能采样。实际32人CPU/GPU、夜景、墙体遮挡、多分辨率及公网多人仍待。

完整构建和回归完成后，工作区另有9项CoF武器生产文件变化，更新方案的完整当前源核验正确拒绝。差异记录为`analysis/night-player-light-controls-20261116/post-validation-source-drift.json`；没有覆盖其他工作，也没有将旧候选宣称为后续整个工作区已验证。当前B／主专服未更新，图形输入仍保持上次Escape停止状态。后续需在共享源稳定后构建一致配套再部署。

在Git Bash中复现，使用新的输出目录及已核验的完整链接封存：

```bash
GOLDCRAFT_TEST_LIGHT_PROVIDER="$(pwd -W)/build/csol-challenge-environment-provider-20261114-01/swds.dll" \
python -X utf8 -B tests/csol-emergency-kick/run.py \
  --seal build/custom-live-final-server-full-20261116-02/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 --sealed-production \
  --fixture tests/csol-challenge-environment/sdk.cpp \
  --groups environment-console environment-flow environment-join environment-replay \
    environment-cancel environment-map environment-missing enabled disabled join rex rex-join \
    failed replay night-failed night-replay breath-male breath-female breath-cancel hud-cancel \
    live-toggle live-expiry live-invalid live-missing live-missing-music live-cancel live-menu \
  --output build/night-light-controls-sdk-new
```

封存头／对象漂移应拒绝并重建，不能为复跑而降低核验。测试调用实际规则、消息和保存链，实体／媒体／引擎边界仍受控，完整疾跑和其他自定义玩法的剩余验收继续保留。
