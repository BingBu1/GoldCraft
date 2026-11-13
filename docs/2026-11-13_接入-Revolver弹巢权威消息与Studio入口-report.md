# Revolver 弹巢消息与 Studio 接线

左轮原角度已接入实际消息收发和 Studio 绘制前入口。此阶段完成传输与消费者，尚未开放购买，也没有将左轮整枪或画面列为通过。

## 原证与宿主适配

匹配 CoF MP `hl.dll`：PE32/x86、基址 0x10000000、SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。原 client.dll 同基址，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。

原 MP 100EFB41..100EFC5D 更新玩家弹巢角；1001ED60 将 pev294 复制到 clientdata472。客户端 1004DD40 和 1004C4A0 复制到本地状态/全局，Studio 1008D21C..1008D2A4 在骨骼计算前执行原控制器数学。复用前重新核验两个完整原样本 SHA，以及既存五个原字节窗的 IDA/PE 一致凭据。原指令/SDK 10206 场景零差证据见前份[控制器报告](2026-11-13_实现-Revolver原弹巢控制器显示-report.md)。

修改前的 evidence→finding→path 在 `analysis/cof-weapons-20261111/revolver-integration-20261113/cylinder-transport/implementation-before.json`。以下均明确为本项目宿主适配：

| 原行为 | 实际源码与适配 |
| --- | --- |
| 传递角度 float，之后才计算控制器 | `native/guest/cof/presentation_protocol.*` 保留 Glock/P345 原有 24 字节 v1；左轮使用 28 字节 v2，末四字节保存角度完整位模式，禁止预量化或推断角度 |
| 原本地玩家状态传输 | `native/guest/cof/server/effects_dispatch.cpp` 将专用消息登记为可变长度，复用同一 session/life/instance/revision 流，避免两个独立流交叉覆盖 |
| 原 viewmodel/武器身份分支及骨骼前应用 | `native/csol/client/weapons/guest_cof.cpp` 读取唯一已接受状态，核对实际连接代次、当前 HUD 左轮身份、真实 GetViewModel；`native/csol/client/plugin.cpp` 在原 StudioDrawModel 前调用原消费者 |

没有生成 firstDraw、装填、角度或时钟默认值。未携角度的左轮消息、其他枪携带左轮角度、截断/超长/错误版本均拒绝且保持输出。普通枪仍发送完全相同的 v1 内容；切到普通枪/明确 release 清掉角度，死亡/重连沿用既有权限失效。真实左轮运行生产者和冷准入仍待后续接通。

## 验证与性能边界

- `build/cof-revolver-transport-protocol-20261113-01/receipt.json`：198883 检查零失败；新增独立字节夹具、2048 个原浮点位模式、两版本切换、旧地图/生命/库存实例、重放与事务拒绝。包括负零、subnormal、NaN payload 和无穷，不在传输层改变原 x86 控制器输入。
- `build/cof-revolver-transport-client-20261113-02/receipt.json`：实际 MetaHook/SDK、生产消息消费者和 plugin TU 编译，165 检查零失败；核对 72/144 度的实际六控制器写入、第四控制器保持、其他实体/武器拒绝、连接变化在下一 HUD 更新前拒绝、release/重生/video/reset。
- 首轮客户端链接缺两个较新 session 的非 CoF reset 边界。夹具补齐同类外部 reset 声明后完整重跑；首轮链接失败记录保留，未改生产代码绕过。
- `build/cof-revolver-transport-server-20261113-01/receipt.json`：256 原声音、128 原 shell/recoil 场景及 7128 实际 SDK 检查零失败；真实可变消息登记、两种长度、发送中重入/源变化/每个取消边界、实体回收与缺角度拒绝均覆盖。
- `build/cof-revolver-transport-mouse-20261113-01/receipt.json`：707 实际鼠标 SDK/thunk 检查零失败，共用机瞄链未退化。

左轮消息体由 24 增至 28 字节，使用固定栈缓冲区；可变 user message 另有引擎的一字节长度，相比原固定长度消息，普通 v1 总传输多一字节、左轮多五字节。绘制入口不分配、不查资源、不访问磁盘/网络，只做身份门和已证标量数学/六次写入。保持 C++20/O3/ThinLTO/AVX2/precise、FP contraction off。没有完整新 DLL 部署或前台实机 CPU/网络/画面/多人验收，不以组件执行时间代替性能通过。

## 下一步与共享文档交接

继续左轮运行时、所有效果事件、原生命周期种子、准入/购买/HUD、VP70 和其余全范围。P345 下载闭包的配套部署及真实 57/57 HTTP 已通过，单 B 自动下载/整枪待验，详见[下载修复](2026-11-13_修复-CoF资源自动下载闭包-report.md)。

PROJECT_STATE/PROGRESS 记本阶段为源码与组件/SDK验证；GOAL 保持全部武器/变体；TODO 保留左轮整枪与实际游戏边界。按人类要求不发送非打包跨聊，精确交接已写入领域文档，不覆盖协调者共享文档的并发改动。
