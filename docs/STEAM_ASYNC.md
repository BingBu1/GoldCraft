# Steam 查询异步化候选

用户需要保持 VPN/Karing 开启。此候选把已定位的 TAB 头像查询和 K 录音请求移出游戏主线程，保留真实 Steam 登录、票据、服务器认证及原生语音编码。没有修改代理分流，没有 Steam 模拟器或认证绕过。

已并入 GoldCraft 入口、构建、沙盒初始化/部署和完整包载荷规划；生产 DLL/helper 及最终四组回归通过。尚未部署到 B，不能声称实际 TAB/K/ESC 已修复。ESC 尚无独立调用栈定位；接收语音的 DecompressVoice 和其他 Steam 接口保持原行为。

## 已定位的调用

匹配的 HL10210 `hw.dll` 与 CS `client.dll` 身份见 `native/client/steam_async_10210.json`。TAB 实际等待栈的 `client+0x33463` 对应 SteamFriends017 的 GetMediumFriendAvatar；后续同一图片路径还读取 GetFriendRelationship、GetImageSize 与 GetImageRGBA。旧代码在比较头像缓存之前就同步查询 Steam。K 实际等待栈的 `hw+0x234d3b` 对应 SteamUser023 的 GetAvailableVoice。开始／停止和 GetVoice 的参数及调用顺序亦已核对实际引擎。

接口 ABI 对照 Steamworks SDK1.61（上游镜像修订 `1b9765621fddbaea0ab68cd4bbdaf9a99c207b72`）中的 SteamClient021、SteamUser023、SteamFriends017、SteamUtils010。只手写所需接口签名，不发布第三方 SDK 或游戏二进制。运行入口通过 MetaHook ResolveGameSymbol 读取已验证的三个 context，保留原初始化；不把 ReHLDS 地址用作客户端地址。

## 线程与生命周期

- 主线程只提交合并请求、读取已完成缓存。头像图片读使用主线程快照，提交和语音读取使用 try_lock，绝不等待工作线程或 Steam 回复。
- 两个独立工作通道分别处理头像和麦克风。每个通道启动一个隐藏的 `GoldCraftSteamWorker.exe`，它通过真实 Steam 客户端连接当前已登录用户。SDK 明确禁止管道创建／释放与其他 API 并发；因此不在游戏进程后台线程调用这些生命周期函数。
- 头像最多缓存128项，最大184×184 RGBA；虚拟图片句柄仅供匹配 CS 客户端使用，其他模块保留原接口。原始上下文一旦就绪即安装适配器，无需等待 helper 初始化；首个查询可以先读空缓存。
- 音频最多16包，每包压缩与原始缓冲各64 KiB，500ms以上结果丢弃，计时从阻塞请求开始前算起。松开 K 后继续交付本次尾音，直到 Steam 返回 NotRecording；NoData 仍须继续轮询。新一次按 K 隔离旧录音并结束旧尾音轮询，即使它持续返回 NoData 也能开始新录音。helper 初始化或传输失败返回 NotInitialized，不回退到主线程同步查询。
- 退出只在主线程发取消信号、恢复钩子。父工作线程使用可取消的管道 I/O；独立 Job 只拥有自身 helper，异常或超时结束该 helper，不结束 Steam 或游戏。工作线程持有插件引用，通过 FreeLibraryAndExitThread 安全释放。

Steam 自身或网络长时间无响应时，头像可以暂未加载、语音可以延迟或缺包；异步化不把这种等待转移回渲染／输入线程。是否还有其他主线程 Steam 阻塞，须以保留 VPN 的实测继续定位。

## 验证与待办

最终传输源码已通过真实独立子进程测试：初始化／请求阻塞时调用方仍能返回、取消退出、原语音字节及延迟尾音交付。测试 helper 不使用 Steam 或麦克风。

最终集成版重新通过四组 CTest：核心阻塞/缓存/尾音/重新按键/缓冲、实际 x86 ABI/首次查询/调用者隔离/回滚、真实独立测试子进程传输/取消、插件 DLL 卸载。主调度复查并修复了旧尾音一直 NoData 阻塞新录音的问题。使用真实 GameData 实现读入新增目录后，三个精确模块的 Steam 全局符号可查询，错误模块被拒绝，已有模型符号保留。第一次目录回归使用了缺前缀的旧符号名称，断言失败已保留；改为生产发布名称后通过。

部署脚本 ValidateOnly 确认 DLL、helper、两份目录文件及三份 shader 共7项，4项需要更新，没有写运行目录。实际完整包规划器在小型不可玩夹具中确认 helper 被纳入载荷但不进入插件加载列表，缺 helper 或目录快照均拒绝。未生成新的用户安装 ZIP、未运行安装器或游戏。真实 Steam helper 初始化沿用此前证据，未启动麦克风；这不是进服、语音互通或卡顿验收。

构建 GoldCraft 目标会同时构建 `GoldCraftSteamWorker.exe`。部署必须一起携带 helper 与 `goldcraft-steam` 目录；初始化、普通部署和 BridgeOnly 路径均已接入，完整包规划器要求它们齐全。下一步联合部署后，保持单 B 和 VPN，实测首次／重复 TAB、按住／松开／再次按 K、实际收发语音、ESC、普通服务器、重连与自然退出。原始证据位于 `analysis/steam-responsive-20261016`，本次整合证据位于 `analysis/steam-integration-20261016`，均不纳入公开仓库。
