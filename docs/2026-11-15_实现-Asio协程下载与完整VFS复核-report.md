# Asio 协程下载与完整 VFS 复核

2026-11-15。本阶段已更新本地单 B 并实际入服。正式服务器没有更新，也没有通知其他对话。

## 已部署的内容

- 独立 Asio **1.38.2**＋Windows WinHTTP；不依赖 Boost。Asio 提供 `awaitable`、`co_spawn`、completion token、IOCP 和重叠文件写入。没有自写 coroutine promise、awaiter 或调度器。
- 原生下载默认同时传输 8 个文件、上限 16 个；资源 SHA 修复保持 2 个并发、最多 4 个未完成任务。并发数不再等于自有等待线程数。两条传输保持各自原有的重试次数、大小/长度规则、取消和进度发布接口。
- 每个队列一个 Asio I/O 线程；不能异步调用的 `FlushFileBuffers`／BCrypt 使用一个按需创建的 Asio 后台线程。WinHTTP 仍使用其系统线程池，不能声称整个进程只有一线程。
- WinHTTP session 按需建立并复用，保留系统 HTTPS 证书校验，禁重定向、自动认证和 cookie。保留操作超时与总超时。取消后等待 `HANDLE_CLOSING`，会话卸载等待 `WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT`，读取缓冲区保持到关闭完成。
- 最后一次校验重新通过真实 VFS 解析**每个名字**；不再以旧文件租约代替 VFS 查找。仍复用未变文件身份的 SHA，保留后台打开/哈希、优先身份任务、挂载祖先检查复用和无变化不重挂载。
- 同步部署已核对的角色模型延迟登记候选和它的配套符号目录。只在整个清单 SHA 校验成功后对 `models/player/<目录>/<模型>.mdl` 启用，`gc_precache_lazy_players 0` 关闭；普通连接保持原路径。

这些是宿主网络／加载适配，不宣称是原 CSOL 的网络实现。

## Evidence → Finding → Path

| 证据 | 观察与结论 | 实际源码路径 |
|---|---|---|
| E-01：`build/vfs-shadow-old-20261115/case-shadow.log` | 旧 `rvk3/resource_update.obj` 对第一次检查后新增的高优先级同名旧文件错误放行；同一真实 VFS 夹具用新对象通过。固定租约只能锁定内容，不能锁定名字的胜出路径。 | `native/client/resource_update.cpp`；`tests/resource-update/main.cpp` 的 `shadow` 场景 |
| E-02：`build/asio-resource-download-20261115-10/result.json`、`build/asio-parallel-20261115-06/{transport,bridge}/transport.json` | 14 组 SHA 下载、94 项原生 HTTP、46 项桥接测试通过；包括坏 SHA、截断、404、重定向拒绝、重试、取消、下一批和反复关闭。 | `native/client/resource_download.cpp`、`native/parallel_download/transfer.cpp`、`native/include/goldcraft/async_download.hpp` |
| E-03：`build/asio-network-performance-20261115-01/result.json` | 三轮交错真实 HTTP、进程/线程 CPU 和 I/O 计数；16 路并发实际达到峰值 16，32 个文件约 526 ms；自有 I/O 调度线程 1，另有后台最终处理线程。不是游戏帧率测试。 | `tests/parallel-download/performance.py` |
| E-04：`build/vfs-final-performance-20261115-02/result.json` | 同一语料、真实 FileSystem009、31 Hz 调用节奏的三方各三轮对照。恢复全量复核增加成本，但大部分优化保留。 | `tests/resource-update/performance.py` |
| E-05：`analysis/asio-download-20261115/lazy-ida-review.json`、`lazy-bytes-verified.json` | 精确宿主 `hw.dll` SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，x86、基址 `0x10000000`。复核 462 条 IDA 指令、5 个窗口 1478 字节与实际 PE 一致。 | `native/client/precache_client.cpp/.hpp`、`precache_10210.json` |
| E-06：`build/asio-client-deployment-20261115-01/{deployment,first-join-status,protected-after}.json` | 六文件配套更新；真实入服 3360/3360、6720 次 VFS 打开、`leased=0`、172 个模型延迟登记，错误为空；画面已见灾变 HUD 和 7500 金钱。 | 本地 B 的 GoldCraft／ParallelDownload 与 `goldcraft-precache` 目录 |

角色加载调用路径 P-01：宿主 `0x101A462B` 检查 `fs_lazy_precache` → `0x101A4653` 调用 `Mod_FindName(1,name)` → 返回值写入模型表；普通分支 `0x101A4662` 调用 `Mod_ForName(name,0,1)`。`Mod_ForName` 位于 `0x1023E930`，先查名再进入 `Mod_LoadModel 0x10240290`。实际消费者 `CL_ModelForIndex 0x101A2FA0` 检查 `needload` 为 1／2 后调用加载器。这些是宿主地址，不能套到其他 `hw.dll`。预缓存 CALL 的参数字节与相邻原生 lazy CALL 在安装前一起验证。

初次误选的旧 `c7bddfda…` 样本、未定义代码导致的错误反汇编，以及把 `0x101FF610` 当成模型加载器的试读均已排除，证据记录明确保留撤回说明；没有据此写入新游戏行为。

## 性能结果与限制

同条件离线语料：3345 文件，445 错件由缓存修复，每个 16 KiB、5 个空 GAME 搜索路径，31 Hz，每版 3 轮，中位数：

| 版本 | 首次校验 | 重连 | 同图新连接 |
|---|---:|---:|---:|
| 原优化前 | 8916 ms | 8602 ms | 6967 ms |
| 租约跳过部分最终解析 | 1837 ms | 1655 ms | 1023 ms |
| 保留优化＋完整最终解析 | **2528 ms** | **2240 ms** | **1937 ms** |

恢复全量解析后首次校验相对原优化前约降 72%。相对跳过版本增加约 691 ms；这是这份离线语料的结果，不推算正式服或所有机器。第一轮对照误用不支持 `join` 的 `rvk0/resource-check.exe`，失败回执保留于 `build/vfs-final-performance-20261115-01`，最终改用报告指定的 `rvk0/b/bench.exe`。

真实小文件/故障 HTTP 对照：原版三轮中位进程 CPU 109.375 ms，协程版 31.25 ms；耗时中位约 1621→1566 ms。CPU 计数粒度 15.625 ms，样本少、存在波动；不能声称协程提高了网络带宽。本轮未测磁盘压力下极端取消延迟、远端 HTTPS/代理及多人同机负载。

本地 B 本次进程首次入服：清单已增长为 3360 个必需文件，461 个缓存修复、0 个 HTTP 下载，完整校验 **2772 ms**；原生恢复 **9438 ms**，模型加载累计 studio **6546 ms**、sprite **1064 ms**，172 个模型登记未立即加载。与昨天 3345 文件的记录不是完全相同输入，不能将差值全部归因于延迟模型。首次出现的新人物仍需原生同步加载；本轮未完成多人同时首次出生的卡顿验收。

## 构建、部署与真实检查

- 独立 Asio 1.38.2 归档 SHA-256：`9f2648fa483e58a6bf848d970ee0ea650ca19ed7769dfa520ed4f7b8d27af1db`。官方 `README` 标明 2026-07-19 发布；仅下载头文件与许可证到工作区构建缓存，源码发布不包含依赖副本。
- `build/asio-goldcraft-full-20261115-01/receipt.json`：1600 输入前后稳定，完整 GoldCraft DLL 成功。预缓存 184 项、资源桥接 17 组通过。
- `build/asio-parallel-20261115-06/receipt.json`：1237 输入前后稳定，DLL、桥接、网络测试和独立协程 ABI 程序构建成功。ABI 三探针通过。
- `build/vfs-full-final-20261115-02/result.json`：20 组真实 x86 FileSystem/HTTP 回归通过。新 `shadow` 在旧对象失败、恢复全量解析对象通过。
- 部署前权威服 `tools/csol_hud_resources.py` 完整 HUD 闭包 310 项通过；没有覆盖客户端刻意保留的错件。真实进服 v4、缓存与 HUD 画面已经确认。
- 已装 GoldCraft SHA-256：`59498c0b949d3f7f6a84f633c95a4b7fea1d13e020b86ae26d03f22d627e1137`。
- 已装 ParallelDownload SHA-256：`00affa3610e18656e92a087b687c3a8407d893a1e091a9a0e8a9246358aa1184`。
- 配套预缓存目录 159 符号、138 指令核对窗口；只换 DLL 而保留旧目录不是受支持的部署方式。
- 受保护 Steam 安装四个核心文件在部署和入服后 SHA 保持一致。专服未重启，本次没有改变阶段测试配置或开启保护。

早期错误路径候选曾在 x86 崩溃，未部署；CDB 和失败测试记录保留在 `analysis/asio-download-20261115`／`build/asio-resource-download-20261115-01..07`。最后方案将 WinHTTP 直接适配为 Asio completion-token 操作，使用明确错误码；所有普通结果都等待关闭。不能把初次崩溃笼统归为编译器错误，独立 ABI 测试并未支持这一结论。

## 复测命令

从项目根目录使用 Git Bash，输出目录必须不存在：

```bash
CLANG_CL="$(python -c 'import json; from pathlib import Path; print(Path(json.load(open(".tools/clang-toolchain.json"))["root"])/"bin/clang-cl.exe")')"
python -X utf8 -B tools/Prepare-Asio.py
python -X utf8 -B tests/resource-download/run.py \
  --output build/resource-download-recheck \
  --compiler "$CLANG_CL" \
  --environment build/csol-season2-trace-20261109/compiler-env.json
python -X utf8 -B tests/resource-update/run.py \
  --output build/resource-vfs-recheck \
  --compiler "$CLANG_CL" \
  --environment build/csol-season2-trace-20261109/compiler-env.json
python -X utf8 -B tools/Build-ParallelDownload.py \
  --output build/parallel-recheck \
  --environment build/csol-season2-trace-20261109/compiler-env.json
```

所有源文件直接从项目编译；不再为资源下载测试复制整份源码。提交仅包含下载组件、调度/缓存改动、测试和工具；整合 DLL 同时消费其他领域尚未独立发布的桥接／预缓存改动，不能用本阶段提交声称整个工作树都已发布。完整大灾变 goal、CoF 奔跑实际 SDK 的两个未通过项、永夜最终画面和所有多人验收继续保留。
