# CSOL 缓存容量与淘汰路径

2026-11-07。只读追踪原版分配链，未改生产代码、构建、部署或发布。匹配的 x86 原版使用独立且固定容量的缓存堆；与 64 MiB hunk 分开，并非为每个模型另行 malloc。专服初始化将缓存容量夹在 14–80 MiB，非专服夹在 14–256 MiB。本结论补齐参数传递和实际边界消费者，不能仅凭初始化常量推出。

清理后复核说明：下文 build 目录的两份 capacity JSON 和原验证脚本已随用户授权清理删除；其 SHA/结果只记录当时核验，不能作为当前可读材料。已从存活 IDA 会话重读启动容量、HeapAlloc、Host_Init 参数、Memory_Init、Cache_Alloc 和清理等核心块，保存至 `analysis/map-transition-active-20261107/original/character-evidence-v2.json`，并与同 SHA 的保留原模块复核。新覆盖以53段实际清单为准，不冒称是旧10段的完整备份。当前复核为 `python analysis/map-transition-active-20261107/original/verify_characters.py`；身份与复核边界见[专服代理模型报告](2026-11-07_取证-CSOL专服代理模型与换图负载-report.md)。

## 身份与原始证据

原版 `hw.dll`：PE32 x86，映像基址 `0x37100000`，SHA-256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。工作区副本与先前模型缓存取证为同一文件。私有 IDA 会话 `original-cache-capacity-20261107`；旧 worker 已过期，本次重新打开工作区 IDB，没有启动或修改原游戏。

`build/map-transition-stall-20261106/original-cache-audit/capacity-evidence.json` 保存此次 IDA 读取的 10 段原字节，SHA-256 `f294abaf7080f767d45125a29283bd636af8aa6cd40b878c33c4bf6160507d59`。`capacity-verified.json` SHA-256 `079b6150d811b1873350c6cfea8b22cece552029234ab432ceeaacf87c2c9ed0`。10/10 段、2,544 字节与确切 PE 一致，所有字节都能从记录的指令边界完整解码。库中未定义函数较多，自动 xref 缺失不能作不存在调用的证据；本次额外核对 PE 的相对 call 与 IDA 原字节。

## Evidence → Finding → Path

| 证据 | 已证事实 | 后续消费者 |
|---|---|---|
| 启动函数 `0x37941238–0x379412A7`；独立初始化函数的同分支 `0x37941DF7–0x37941E4B` | dedicated 标志 `0x39414C70` 非零时取 14 MiB 下界、80 MiB 上界；零时上界 256 MiB | 选定值在 `0x379412D3` / `0x37941E71` 写入 `0x39A7FD9C`；`-minmemory` 另令其为 14 MiB |
| `0x379412C1/0x379412E3/0x379412E9` | 第一块 64 MiB 用 `HeapAlloc`，保存到 `0x39A7FD90` | hunk base/size 对应参数结构的 +0x10/+0x14 |
| `0x3794131D–0x3794132F`；独立同路径 `0x37941EBF–0x37941ED5` | 第二块按 `0x39A7FD9C` 容量 `HeapAlloc`，保存 base 到 `0x39A7FD98`；失败字符串明确为 cache heap | cache base/size 对应参数结构的 +0x18/+0x1C。IDA imports 将 `0x3800F4C0` 识别为 KERNEL32 `HeapAlloc` |
| `0x37941449/0x3794144E` | 启动函数把 `0x39A7FD80` 参数结构传入 `Host_Init`（`0x378A0140`） | 刚保存的两块堆都位于同一结构，未互换 |
| `0x378A01AC–0x378A01B8` | Host_Init 将 +0x1C/+0x18/+0x14/+0x10 依次压栈，调用 `Memory_Init 0x3798C5A0` | cdecl 第 3/4 参数正是 cache base/size，第 1/2 参数是 hunk base/size |
| `0x3798C5BF–0x3798C5EC` | cache 第 3 参数写 `0x39424290`；base+第 4 参数写 `0x3942428C`；第 4 参数还写 `0x39424288` | 得到实际 allocator 使用的 cache 起点、终点、容量。hunk 分别写入 `0x39424354/0x39424350` |
| `Cache_Alloc 0x3798AE70`，特别是 `0x3798AEB5/0x3798AF30/0x3798AF51` | 申请量加缓存头和对齐后，在固定 base/end 间查可用空洞；非空尾部分配显式比较申请末尾与 end | 没有每个模型单独调用 malloc 或 HeapAlloc，也没有扩张 arena 的成功分支 |
| `0x3798AFAA–0x3798AFE1` → `Cache_Free 0x3798B2E0` | 找不到空间时，取 LRU 尾的 owner，摘除缓存链/LRU、清 owner.data，然后重试；空 LRU 时报 out of memory | `0x394242B0` 为此路径淘汰的链尾，`0x39424278` 记录淘汰数；保留真实淘汰语义 |
| 独立 `Cache_TryAlloc` 形态函数 `0x3798BC30–0x3798BD67` | 同样读取 cache base/end，越过 end 返回 0；未发现 HeapAlloc 或无界扩张 | `Cache_Alloc` 已内联同一逻辑；本报告不把该独立函数假称成已找到直接 caller |
| `0x3798B130` | 另有在同一 arena 内移动对象并更新 owner 指针的 compact 函数 | 本次未建立它在实际换图时被调用的证据，不能解释成原版换图必定主动压缩缓存 |

调用路径为：选定 dedicated/client 容量 → 分配独立 cache heap → 参数结构 → Host_Init → Memory_Init 设置 base/end → Cache_Alloc 查空间 → 满时淘汰 LRU 并重试。

因此此前“80/256 MiB”数值确实会成为这一启动路径的 cache 容量，而不是仅影响 hunk 或另一个无关参数。此证明仅针对匹配的原 x86 样本；不外推当前 CSOL x64、其他版本或所有资源系统。

## 与当前性能任务的关系

父任务现场回报：当前 prefix 候选同图仍分配 824 次、最终 122 个条目，热加载 10.377/11.025 秒，未优于此前 7.877 秒样本。该计时由父任务采集，本子任务没有重复操作游戏或独立采集；因此只把它作为继续诊断的输入，未给出统计显著性或提速比例。源码安全审查通过不能据此关闭性能任务。

下一步须检查当前真实模型集里哪些模型未通过 prefix 资格、前缀总量及重复转换占用；原版已证的独立固定 cache 不能支持“原版无限 malloc 所以不淘汰”的猜测。准备中的公开候选已经暂停，不执行 stage、commit 或 push；后续生产改动需重新对应证据并通过实测。

清理前历史复核命令为 `python build/map-transition-stall-20261106/original-cache-audit/verify_capacity.py`，现已不能执行；当前命令见本文开头。原模块、IDB、原字节、反汇编和实验构建数据均不属于源码发布内容。
