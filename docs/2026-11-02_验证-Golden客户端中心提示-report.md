# 黄金僵尸中心提示

独立 CSOLSupport 现有中心提示已支持黄金僵尸出现和击杀消息。实际客户端代码的专项测试完成 **181 项检查、0 失败**。本次仅提交源码和测试，不包含运行 DLL、服务端生成接线、游戏资源或部署；真实字体、最终像素和多人网络尚未验收。

## 行为与原版依据

原消息使用固定 TextMsg 正文：四字节 `{4,0,7,0}` 后跟 `#CSO_ZBS_SpawnedGoldenZB` 或 `#CSO_ZBS_KilledGoldenZB` 及终止零，长度分别29、28字节。第三字节7选择黄金颜色；第四字节0沿用 `scr_centertime`。消息没有玩家名或身份参数，不排队，也没有已证的额外提示音。

本地保存的 IDA 导出经原复制模块和指令边界核对，10段共5044字节一致；证据索引为 `analysis/csol-model-20261017/scenario/golden-notice-client-20261102`。当前中文目录中两条文案为“不好！黄金僵尸出现了！快消灭它们！”和“恭喜！成功消灭了黄金僵尸！”。目录 SHA 为 `c5d675c7565b8694536fc2496741c8f6f38ed06f2a717d4476102b19400c6a4e`。原模块、分析导出和游戏资源只保存在合法本地环境，不随源码发布。

| Evidence | Finding | Path |
|---|---|---|
| E1：原 MP/client 字节与历史 IDA 指令核验，verification SHA `17889bdbcdb9477ac4a3606304443cd7ce113337831e39535f91a9d137fdd205` | F1：固定key、黄金颜色和继承cvar时长；复用单一中心对象 | TextMsg → `DecodeGoldenText` → `Text` → `State.SetCenter` |
| E2：完整中文目录及原绘制代码 | F2：模式15位置 `int(H*.614583)`，背景宽60%、高33，继续使用 `EngineFont` | `LoadLocalization` → `FormatCenter` → `Paint` |
| E3：prepare-03真实消费者181/0 | F3：消息归属、替换、期限和实际绘制API调用符合专项用例 | 真实hook → 实际生产TU → 受控ISurface边界 |

F1/F2 为静态结论；F3 为受控客户端代码运行结果。黄金色由原 float `(1,.843,0)` 取最近整数为 `(255,215,0)`；原引擎最终像素如何量化仍未单独验证。测试供应受控字体句柄和字宽，不能证明真实字体命中或字形效果。

新消息沿用现有中心提示、背景和生命周期，后到的复活提示恢复普通橙色。另明确支持标准 CS 单字节 destination 加固定key的适配包，便于服务器使用原 TextMsg 格式；这并不代表没有 CSOLSupport 的原版客户端会显示同款界面。

专用解码器是 Golden token 的唯一接受入口。可识别的坏 Golden 布局不覆盖旧状态、不落入原解析器。无法可靠区分的控制前缀继续转发原链，但普通解析分支只允许旧五种提示，不能重新接受 Golden。普通文字前置、嵌入或带 Golden key 后缀均保留原链。

## 验证和复现范围

使用 `tests/csol-golden-notice/client.cpp`，编译完整实际 `scenario_respawn_ui.cpp` 与本地化解析器，再通过真实消息hook触发消费。四条命令全部退出0，总19.3898秒；客户端521个、解析器228个实读头，共521个唯一头均核SHA，输入终检保持。日志保留六项SDK警告和一项测试循环拷贝警告，没有生产代码警告；没有为消除警告重复运行。

检查覆盖两原始包、标准适配、完整中文目录、负/零/正常/非有限时长、Golden与复活相互替换、错误头与参数、模糊控制前缀、普通文字、三种分辨率、可见性门、缺字体、ResetHUD/VideoInit/InitHUD/会话切换/退出。绘图过程只记录API调用；专项不执行图像加载，不读取额外图片。

本地结果为 `build/csol-golden-notice-20261102/prepare-03/result.json`，SHA `a41eeb41acec0473427b383628c8957a5f4cb47fb2a28c76364ea219feb70871`；实际日志输出 `golden_notice_client checks=181 failures=0`。主调独立核收回执为 `build/coordination-20261102/golden-notice03-result.json`。

prepare-01/02 均未运行：先修正普通文字前缀误截，再修正通用解析分支重新接受组合坏头。两个原提案保留，实际只执行最终03。准备入口及本地工具链前置见[测试说明](../tests/csol-golden-notice/README.md)。准备器依赖本机此前保存的已核UI03工具链记录和合法资源；公开仓库不包含这些私有数据，不能称克隆即跑。

本次六项最小发布集合的项目依赖已与 main9c1f2d2 比较：其余已冻项目源码内容相同，仅两项有换行差异。公开最小集合没有另行重新编译。后续仍需服务端发送的完整链路、全 DLL、真实客户端字体/像素及多人验收；181/0不替代这些目标。
