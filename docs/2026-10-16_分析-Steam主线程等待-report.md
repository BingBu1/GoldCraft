# Steam 主线程等待与异步候选

TAB 与 K 的同步等待已定位；主调度已整合异步入口、helper和交付脚本，最终四组回归通过，尚未部署到 B。VPN 保持不变。此结论不包含 ESC 修复、真实语音互通或游戏帧率验收。以下 E1–E5 保留原领域交接时的历史验证范围；最新集成证据见 `analysis/steam-integration-20261016` 和 [STEAM_ASYNC.md](STEAM_ASYNC.md)。

范围为用户授权的游戏响应修复；本地范围与交接见 `analysis/steam-responsive-20261016/{scope.md,HANDOFF.md}`。只分析工作区模块副本，不改原安装或认证。

## 证据

| Evidence | source_ref | 核验／复现 | content_hash |
|---|---|---|---|
| E1 | 既有 `analysis/client-responsiveness-20261015/tab01/stack02.txt`；本轮 `tab-request.txt`、`tab_row.json`、`avatar_image.json` | IDA对匹配client.dll反编译0x10033380与0x100ade60；将实际加载基址换为RVA。原现场只可离线复核，不可伪造新实测 | 见本地candidate-manifest.json evidenceHashes |
| E2 | 既有voice-k01栈与voice-wait-review01报告；本轮voice_poll/start/stop.json | IDA对匹配hw.dll反编译0x10234cb0、0x10235520、0x10235640 | 同上 |
| E3 | 本地SDK1.61头与161-sources.json | 核对SteamClient021生命周期线程限制及User023/Friends017/Utils010的接口顺序；上游修订1b9765621fddbaea0ab68cd4bbdaf9a99c207b72 | 同上 |
| E4 | build10.log、transport03.log、lifetime02.log | 本地build-tests.ps1、build-transport-tests.ps1、build-lifetime-tests.ps1；Clang x86真实源码。仅transport03覆盖最终状态机，其他两份为修正前版本 | 同上 |
| E5 | steam-smoke05.log | 本地smoke.exe启动真实helper，使用系统已加载Steam目录的原生绝对路径；未调用开始录音。该测试须已有真实Steam登录 | 同上 |

候选元数据校验可在项目根目录运行：

```powershell
python tools/Build-SteamAsyncGameData.py --engine analysis/steam-responsive-20261016/hw.dll --client analysis/steam-responsive-20261016/client.dll --output analysis/steam-responsive-20261016/gamedata
```

命令只接受工作区内与已审计 SHA256/CRC64 一致的模块，逐项检查签名和指令操作数，不向游戏写内存。

## 结论

| Finding | severity | evidence_ids | status／confidence | location／影响 |
|---|---|---|---|---|
| F1 | n/a_re | E1 | validated／high | client+0x33463的GetMediumFriendAvatar在检查缓存前同步请求；头像图片和好友状态路径也须覆盖 |
| F2 | n/a_re | E2 | validated／high | hw+0x234d3b的GetAvailableVoice进入Steam同步Event等待。现有证据不支持“mutex死锁”或“实测等待90秒” |
| F3 | n/a_re | E3 | validated／high | SteamClient管道创建、连接、释放不能与其他API并发；候选改为独立进程管理管道，未部署被替换的线程内管道方案 |
| F4 | n/a_re | E4、E5 | candidate／high | 主线程缓存、后台请求、原音频字节、代次隔离和取消/卸载已有局部证据；真实Steam初始化211.58ms通过，但游戏效果仍待集成验证 |

## 调用与数据路径

P1，path_type=callflow：实际 TAB → CS战绩栏 → GetMediumFriendAvatar → Steam IClientFriends同步请求 → Event等待。各步由E1支撑，结论F1。

P2，path_type=callflow：实际 K → hw语音轮询 → SteamUser023 GetAvailableVoice → IClientAudio同步请求 → Event等待。各步由E2支撑，结论F2。

P3，path_type=callflow，候选：主线程适配器提交合并请求／读取缓存 → 两个独立工作通道 → 可取消管道 → 各自Steam helper → 完成结果 → 主线程消费。E3支撑管道生命周期隔离，E4提供分版本的阻塞响应、ABI、回滚与卸载证据，最终尾音已在真实子进程传输中验证；首次查询不等 helper 的适配器更新待重跑。E5只验证真实连接初始化。认证和接收语音解码保持原实现。

## 经过与剩余验证

2026-10-16依次完成已有栈精确定位、SDK ABI核对、缓存/录音候选、线程限制修正、进程隔离、取消/卸载测试及真实Steam初始化。早期缺少lld/x86目标配置的编译失败、前斜杠测试路径导致LoadLibraryEx错误126的失败均保留；生产路径来自GetModuleFileNameW。没有通过改代理隐藏问题。

最终审查补两处边界：首次适配不再等 helper 就绪，避免早按 TAB 漏回同步路径；SDK 明确要求 StopVoiceRecording 后继续 GetVoice 至 NotRecording，故松键保留本次尾音、新按键才丢弃旧代次。transport03 验证两个 NoData 后仍交付尾音；transport02 在 Windows PowerShell 5 缺少 Avx2 类型时预检失败，未开始编译，改用项目使用的 PowerShell 7 后通过。共享源码、部署和 Git index 未在本轮修改。

主协调整合后补修“旧尾包无限NoData阻止新录音”，重新运行最终核心/ABI/传输/卸载四组，并通过实际GameData合并/模块隔离与helper载荷规划。之后必须保持VPN开启，验证首次及重复TAB、K真实语音收发和松开/重按、ESC、普通服、重连与自然退出。ESC未获独立调用栈，不因TAB/K定位而推定相同原因。源码、编译、自动测试、真实初始化与游戏验收分开记录，完整说明见 [STEAM_ASYNC.md](STEAM_ASYNC.md)。
