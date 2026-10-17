# ESC 菜单的 Steam 等待调查

2026-10-17 最终真人结果：用户对ESC开关菜单问题回复“已经不卡了”，随后确认“我重装了Steam好了”。本次ESC卡顿已结案。下面的空闲采样记录保留其原始边界，最终恢复依据为用户实测；没有新部署游戏层ESC补丁。真实语音、普通服、重连和长期在线仍分别验收。

ESC 卡顿已取得双进程证据：游戏主线程等待 Steam Input 查询或好友状态发布，Steam 的 IPC 处理线程同时进入额外加载的 `Inject DLL.dll` 并停在网络等待。2026-10-17 重复复现新增 8 个长帧间隔，最大 6745.33 ms。用户自行重装 Steam 后，同一沙盒客户端已重新通过公网身份认证；两分钟空闲对照没有新增长帧，但没有观察到菜单切换，该空闲观察本身不构成菜单验收；随后用户已明确确认ESC恢复。K、TAB 的用户通过反馈不覆盖这些接口。

## 范围与身份

用户要求捕捉 ESC 卡顿、解释 Steam 在做什么，并研究通用处理。授权范围为只读采样沙盒游戏和真实 Steam、静态分析工作区内副本及启动单 B 对照；没有改变代理、认证、公共服务器、Steam 设置或原安装。重装 Steam 是用户操作，本轮未执行卸载或修改 Steam 文件。原始日志、二进制、SDK、账户、地址和桌面恢复资料仅留在本地 build，不随报告发布。

| 对象 | 版本或身份 |
| --- | --- |
| x86 hw.dll | HL10210；SHA256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295` |
| x86 steamclient.dll 副本 | 11.12.30.7；SHA256 `6b73151780c4d48bc5c37c674a69ef49094b5656482335dcb28730f00b646dbb` |
| Steamworks 声明 | SDK 镜像修订 `1b9765621fddbaea0ab68cd4bbdaf9a99c207b72`，SteamInput006 / SteamFriends017 / SteamUtils010 |
| 工具 | CDB 非侵入、不暂停采样；IDA 与 Capstone 静态交叉核对 |

下列地址为以上副本的首选映像地址，不能直接用于其他版本或运行时地址。Steam 主进程可以是 x64，游戏内模块仍为 x86；两者不能混用地址或调试器架构。

## Evidence：可核验观察

| ID | source_ref（本地） | content_hash（SHA256） | 观察 |
| --- | --- | --- | --- |
| E01 | `build/esc-stall-20261016/summary.json` | `ae8d3f2f4d32665d26e3e0ef2a777e08e2eb4ba1aeaa2a0b693ee43ce8ce7be3` | 610 样本，排除首样本已有 6 条历史；新增 29 个 frameGap，最大 5460.93 ms。14 次栈中 9 次 Input、3 次 RichPresence、2 次无法可靠归类。 |
| E02 | `build/esc-stall-20261016/engine-calls-disasm.txt` | `4b8f0ee0a87bb19d93b614a2f5c18939f3476da77af6251d46eb26272137e4d9` | 菜单函数和好友状态函数的实际虚表调用。 |
| E03 | `build/esc-stall-20261016/client-pipe-disasm.txt` | `bf3b6054d5a12364dd5b4400b35c8e2cce9c774722166577a815b23f06d451b3` | `CClientPipe::BWriteAndReadResult` 在等待回复期间持有临界区。 |
| E04 | `build/esc-stall-20261016/pipe-dispatch.txt` | `22aa1025483b6c9f813bcc1c153805b3c81e23473a32ed710632414eaac5b583` | 调度器按调用方提供的 pipe 索引查表，再调用 E03；不是自动按当前线程换管道。 |
| E05 | `build/esc-stall-20261016/steam-api-disasm.txt` | `db18529d905321b75fbeae92a465db19dbc75a780608352fb6d6bf84b58bef6a` | `0x3862a780` 先读本地 action handle 缓存；零值才查询远端；仅非零回复写入缓存。文件的两个任意窗口开头不是指令边界，不作为证据。 |
| E06 | `build/esc-stall-20261016/controller-excerpt.txt` | `bf9cb981b27aa3f9fed44a7eff03df218335df91dfc5157805f8610653579d9c` | 当天 Steam 日志出现 `No cached sticky mapping in GetActionSetHandle`。时间早于 ESC 采样，不能视为每次卡顿的直接日志。 |
| E07 | `build/esc-stall-20261016/observer-replay.json` | `86ac5269e148e691cff6e3fc1c89e3630b40e07a6cd8b798e62c4226d583d1e7` | 新观察器回放同一批 610 样本，六项计数/耗时与 E01 完全一致。 |
| E08 | `build/esc-stall-20261017/paired-findings.json` | `732ca2114e6ff49dd068ed88aaff8cb4d1ba5d9d11238a2bd05b083f0615e8b5` | 用户再次复现：777 样本、8 个新增长帧，最大 6745.33 ms；八对栈均在 Steam 端出现网络等待与额外模块返回地址。游戏端五次 GetActionSetHandle、两次 SetRichPresence、一次未能可靠归类。 |
| E09 | `build/esc-stall-20261017/loader-cache-verification.json` | `103e400f2ac8f06ef3d0f38fbc9f4e981617fccf1ba01de29c97c95ac29897db` | 旧 Steam 缓存中载荷的 `.text` 1675776 字节与 `.pdata` 67072 字节匹配所读内存；导出名称 `Inject DLL.dll`，含 `Global_SteamtoolsIPC_Class`。未执行该载荷。 |
| E10 | `build/steam-reinstall-20261017/esc02/summary.json` | `d02420deabf8b33eefc23f09650d7b4a0a32b230ec8d5a83e7274a495cd5fd03` | 重装后同一沙盒版本的 1179 样本、120 秒空闲对照，新增长帧和菜单切换均为零。不得当作 ESC 通过。 |
| E11 | `build/steam-reinstall-20261017/checkpoint.json` | `52a552f88a52eae675e12c193adf1fb2691347322cdf9d85f7594c52d7608e75` | 三次公网只读状态保持同一真实身份/连接，末次在线 6 分 37 秒；两辅助通道 AppID0、已登录、样本新鲜。原安装19575项元数据及5项内容hash保持。 |

复现 E01/E07 需要本地保存的 `status.jsonl`；它未公开，公开仓库不能独立复算该次用户现场。E02–E05 需合法取得上述精确 hash 的模块后，在指定函数入口反汇编；不能从任意字节窗口推断函数。SDK 来自固定修订的公开声明，与实际虚表调用顺序逐项核对。

E08/E10 可用下方观察器命令重新采样，但数值依赖用户实际会话与菜单操作；历史统计需本地保存的 `status.jsonl`。E09 仅供本地核验，缓存载荷和内存段不公开；该文件不能用来执行或安装附加组件。E11 复核需管理员在自己的服务器读取 `status` 并关联相同玩家连接；历史原始身份仅本地保存，公开读者无法独立复算。重装前后 `steam.exe` 和游戏内 `steamclient.dll` 的 SHA256 相同，安装目录、缓存和登录会话已变化，因此这不是只改变一个变量的因果实验。

## Findings：结论及界限

**F01 — 已验证，confidence=high，severity=n/a_re，证据 E01/E02。**
`hw!0x10219440` 在菜单切换中先查询 Steam Deck 状态、更新 VGUI，再执行 DeactivateAllActionSetLayers、GetActionSetHandle、ActivateActionSet；退出菜单进入多人游戏时还激活 multiplayer layer。采样中的真实返回地址为 `hw+0x2194d0`，落在 `GetActionSetHandle` 后。另三次停在 `hw+0x2188eb`，对应 SteamFriends017 slot43 的 SetRichPresence。它更新好友可见的游戏信息；引擎在同一个函数中发布约 11 个字段。

这些请求没有及时返回时，主线程无法继续推进帧。按键处理计时的进程累计最大值为 21.2651 ms、超过 50 ms 次数为零，不能把秒级停顿归为按键钩子本身。帧间隔是整个停顿的观测值，不是每一个 Steam API 单独计时。

**F02 — 已验证，confidence=high，severity=n/a_re，证据 E03/E04。**
`0x387bd000` 的诊断字符串标识 `CClientPipe::BWriteAndReadResult`。`0x387bd0fe` 进入临界区，发请求并在 `0x387bd17e` 等回复，`0x387bd1e8` 才释放。`0x38775fe0` 用显式 pipe 索引查表；只换工作线程不能证明请求使用另一条管道。

等待实现有 5000/180000 ms 分支，下一层将传入时间分半，所以栈上能看到 90000 ms。**这是等待参数，不是观测到的卡顿时长，也不应直接缩短或强行提前返回：上层仍要读取匹配的回复。** 同一 pipe 的其他调用可能被临界区拖住，未经证明不能把普通后台线程称为完整隔离。

**F03 — 缓存逻辑已验证；现场缺映射解释为候选，confidence=medium，severity=n/a_re，证据 E01/E05/E06。**
Steam 自己已缓存非零的 action handle；九次现场栈位于其本地缓存未命中后的远端查询路径。因此“再缓存一次正常返回值”不足以解释或保证修复。缺失映射返回零、初始化/映射状态反复失效是需要继续区分的情况。Steam 端相关日志有一次性输出计数，缺少重复日志不证明后来没有重复失败。

当日单进程证据不能区分 Steam 后台原因；次日 E08/E09 已补充处理线程证据，见 F04。仍未核定网络请求的目的地址、响应内容或具体超时机制，不能把代理作为已证根因。

**F04 — 已验证额外代码参与阻塞路径，confidence=high，severity=n/a_re，证据 E08/E09。**
位置为 Steam IPC 处理线程的额外载荷与 `WS2_32!select` 等待栈；对应内存有 libcurl 调用/诊断特征和上述载荷标识。双进程采样证明游戏正在等待的同时，该 IPC 线程被这条路径占用。旧 Steam 目录的附加 `xinput1_4.dll` 与该缓存载荷之间的加载关系已在本地副本核对；不把 DLL 文件名本身当成异常判据。不能据此断言用户主动安装过哪个工具，也未证明所有网络等待均由相同原因造成。

**F05 — 已验证重装后空闲运行和身份准入，ESC 对照未完成，confidence=high，severity=n/a_re，证据 E10/E11。**
新 Steam 已正常登录，沙盒 B 与两个 AppID0 辅助通道成功连接；只读公网 `status` 确认真实 Steam 身份通过，后续同一连接持续超过五分钟。新 Steam 的已登记模块不含旧目录文件，所加载 `XInput1_4.dll` 来自 Windows 系统目录且微软签名有效；这不等于扫描了所有私有可执行内存。E10 只覆盖无菜单切换的两分钟，不能把零长帧推广到 ESC/K/TAB、真实语音或长期掉服验收。当前仍等待用户菜单对照结果。

## Path：调用链与修复边界

P01（callflow，E01/E02/E05 → F01/F03）：用户切菜单 → 引擎更新 VGUI → SteamInput 本地查 action handle → 未命中后发同步请求 → 游戏线程等待回复 → 帧停止推进。

P02（callflow，E01/E02/E03/E04 → F01/F02）：引擎更新好友状态 → SetRichPresence → 按 pipe 查表 → 临界区内写请求并等回复 → 返回后才能继续引擎循环。

通用原则是让可延迟的展示/设备查询采用有界队列、合并更新和缓存，游戏主线程不等待外部服务。不同接口的返回值、输入状态和生命周期仍需分别适配。当前 TAB/K 的 AppID0 辅助进程不能直接承接属于当前游戏的 Input/RichPresence；不得增加第二个游戏身份、挪用认证票据、搬走 VGUI 操作或绕过 Steam 登录。

P03（callflow，E08/E09 → F04）：游戏发出同步 Steam 请求 → Steam IPC 处理线程进入额外载荷 → libcurl/套接字等待占用该线程 → 游戏等待回复期间停止推进帧。这证明额外代码位于当次阻塞路径，不确定请求具体业务，也不把其他所有卡顿归为该原因。

下一步在用户的新 Steam 会话内完成首次/重复 ESC、菜单进出和 K/TAB 对照；已有接收端证据无需重做。若仍卡顿，再取得新鲜双进程栈后决定接口修复，不直接缩短 IPC 超时或屏蔽调用。还须验证重连、正常退出、真实语音及长期认证；编译或模拟后端成功不能关闭实机待办。

## 双进程只读采样

[Observe-SteamStall.py](../tools/Observe-SteamStall.py) 要求显式 live PID，并持有只查询句柄、核对映像名称/创建时间；游戏必须位于 sandbox。按各自 PE 架构选择 CDB，使用 `-pvr`，两个进程的栈尽量同时采样。状态文件停止更新达到阈值时才采样，默认两分钟、最多六对；只写新的 build 目录，不发送游戏输入、不改 ETW、不重启进程。

在项目根目录的 PowerShell 中：

```powershell
$game = @(Get-Process MetaHook -ErrorAction Stop)
$steam = @(Get-Process steam -ErrorAction Stop)
if ($game.Count -ne 1 -or $steam.Count -ne 1) { throw 'Select the intended single game and Steam process first' }
$capture = 'build/esc-paired-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
python tools/Observe-SteamStall.py --game-pid $game[0].Id --steam-pid $steam[0].Id --output $capture
```

原始栈保留在本机，可能含私有路径。非暂停采样有竞态和开销；栈中的近邻导出名不等于函数真实名称。状态文件停更也可能是其他启动/加载停顿，须结合调用栈和用户操作判断。

观察器 5 项回归通过，覆盖历史排除/去重、滚动丢失提示、跨会话拒绝、输出路径和非暂停命令；已完成真实 x64 Steam 空闲进程采样、八对复现栈及重装后的两分钟空闲对照。本轮重新运行五项回归全部通过。工具可用不代表 ESC 已修复。

## 时间线与当前状态

- 2026-10-16 21:53–21:55：用户复现 ESC；保存 610 状态样本及 14 份 x86 游戏栈。
- 2026-10-16：核对匹配引擎、Steam 接口、pipe 等待与缓存分支；未改生产代码。
- 2026-10-17：复查发现旧游戏进程已结束、Steam 已换会话；不把旧 PID 当活现场。完成双进程观察器及回归，用户随后授权启动原沙盒 CS B。
- 2026-10-17 11:55–11:57：用户复现卡顿，双进程采样补齐 Steam 处理线程；缓存与已加载代码匹配。
- 2026-10-17 13:30：用户自行重装后，新 Steam 正常登录；主调度随后启动同一沙盒 B，游戏 DLL/helper 保持原版本。
- 2026-10-17 13:40–13:42：两分钟空闲对照，零新增长帧、零菜单切换。窗口捕获先提示请求占用，重新选择窗口后仍超时；没有自动发送按键。
- 2026-10-17 13:46：公网同一认证连接已持续 6 分 37 秒，ESC 用户对照仍待。

完整 GoldCraft 目标保持开放。本报告交付双进程定位、可复用采样工具及重装后的有限对照；没有新部署游戏层 ESC 补丁；ESC恢复已另获用户真人确认，空闲观察与进服成功不替代长期掉服或完整玩法验收。
