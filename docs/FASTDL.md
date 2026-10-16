# CS 入服资源 HTTP 下载

`tools/fastdl` 将明确列出的 CS 资源发布成独立 HTTP 目录。普通 CS 使用引擎原生 FastDL；可选的入服前预取工具提供并发、gzip 和 SHA-256 校验。需要 Python 3.11 或更高版本，仅使用标准库。

目前完成源码及匹配客户端静态核验、工具实现、22 项 Windows/Python 3.14 回归、完整命令行流程和实际沙盒资源传输。**普通 CS 实际入服、HTTP 失败后游戏通道回退、取消后重连及公网吞吐尚未验收。** 工具不启动游戏、不部署服务器配置、不修改代理设置。

## 快速验证四个实际资源

在仓库根目录运行以下 PowerShell 命令。示例读取现有沙盒专服的 `cs_assault`、ZP 模型和声音；输出目录必须是新的。四文件清单仅验证工具，不能当作完整服务器资源清单。

```powershell
New-Item -ItemType Directory -Path build/fastdl-example -ErrorAction Stop | Out-Null
@'
maps/cs_assault.bsp
models/zombie_plague/v_knife_zombie.mdl
sound/zombie_plague/zombie_infec1.wav
sound/zombie_plague/zombie_pain1.wav
'@ | Set-Content build/fastdl-example/resources.res -Encoding utf8

python -B tools/fastdl/publish.py plan --root sandbox/cs-server/Half-Life/cstrike --list build/fastdl-example/resources.res --output build/fastdl-example/plan.json
python -B tools/fastdl/publish.py publish --plan build/fastdl-example/plan.json --output build/fastdl-example/release --url http://127.0.0.1:8080/cstrike/r1/
python -B tools/fastdl/serve.py --release build/fastdl-example/release --prefix /cstrike/r1/ --bind 127.0.0.1 --port 8080
```

最后一个命令持续提供服务，Ctrl+C 停止。在另一个终端预取到独立目录：

```powershell
$FastdlSha = (Get-FileHash build/fastdl-example/release/manifest.json -Algorithm SHA256).Hash.ToLowerInvariant()
python -B tools/fastdl/fetch.py --url http://127.0.0.1:8080/cstrike/r1/ --manifest-sha256 $FastdlSha --destination build/fastdl-example/client/cstrike_downloads --workers 4
```

成功退出码为 0，逐文件结果为 `downloaded` 或 `cached`。已存在且内容不同的文件保留，结果为 `conflict`；资源传输失败为 `fallback`，整体退出码 2。清单验证或初始化失败直接非零退出，Ctrl+C 为 130。调用方可继续普通 CS 的连接流程，但预取器本身不连接游戏、不发送 UDP，也不会修复已有冲突文件。

远程预取的 `manifestSha256` 必须来自可信发布回执，不能从同一未验证的下载地址现取现信。此哈希不是游戏协议自动提供的字段。

## 为真实服务器生成完整清单

在目标地图和 AMXX 插件加载完成后，分别捕获 ReHLDS 控制台以下四条命令的完整输出：

```text
reslist model
reslist sound
reslist generic
reslist event
```

每个文件只能包含一次完整命令结果，包括末尾 `N Total ...'s`。使用项目既有控制台取证方式；工具不会向正在运行的游戏发送命令。随后执行：

```powershell
python -B tools/fastdl/publish.py plan --root sandbox/cs-server/Half-Life/cstrike --root sandbox/cs-server/Half-Life/valve --reslist-model build/fastdl-example/model.txt --reslist-sound build/fastdl-example/sound.txt --reslist-generic build/fastdl-example/generic.txt --reslist-event build/fastdl-example/event.txt --output build/fastdl-example/full-plan.json
```

`--root` 按引擎的实际搜索优先级重复传入。若存在 `cstrike_downloads`，将它放在 `cstrike` 与 `valve` 之间；不存在的根目录应省略。实际配置另有搜索根时应明确加入。文件必须可直接读取；工具不自动提取 PAK 中的资源，也不猜测缺失文件。

解析器检查行号和总数，拒绝截断/合并捕获；声音补 `sound/`，内嵌 `*` 子模型和 `!` 句子不当作文件。`decal` 列表是贴花名称，不能直接用于文件清单。显示的两位小数 KiB 和八字符 MD5 不作完整性依据；大小与 SHA-256 从实际文件计算。

当前 `reslist` 临时数组容量是 4096。任何一类捕获达到这个数量都拒绝，须提供完整显式清单 `--list`，不能把可能截断的结果发布出去。显式清单支持 `.res` 风格注释和包裹路径的双引号；必须包括实际 precache、模型附属文件和地图 `.res` 依赖。工具不会仅凭一个地图 `.res` 就宣称收齐所有插件资源。多地图可重复传入对应清单/分批审核后合并显式清单，并在源文件变化时重新规划。

## 发布并启用 FastDL

`publish` 先核对计划内全部源文件，再复制和压缩到独立临时目录，校验后原子改名。禁止覆盖已有版本或输出到输入根目录内。Windows 短暂文件锁最多等待 1.55 秒重试，失败仍不发布半成品。发布结果包含清单哈希、原始/压缩字节数和待审核的配置文件。

发布目录保存原始资源、`manifest.json`、按内容哈希命名的 `_fastdl/*.gz` 和 `server-fastdl.cfg`。`serve.py` 只提供清单中的原始/压缩资源和清单本身，**不提供配置文件、目录列表或其他文件**。

确认 HTTP 地址对玩家可达后，在目标专服的既有部署窗口应用生成配置：

```text
sv_downloadurl "http://127.0.0.1:8080/cstrike/r1/"
sv_allowdownload 1
sv_send_resources 1
sv_allow_dlfile 1
```

示例环回地址仅供同机验证。实际远程部署必须在发布时使用玩家可访问的最终 HTTP(S) 地址，并匹配 HTTP 服务路径；无需、也不应为此修改系统代理。`serve.py` 默认只监听环回；对外监听、TLS/反向代理及公网部署由服务器维护者另行安排，当前未执行。不要把整个游戏目录作为静态站点根目录。

URL 必须以 `/` 结尾，无认证信息、查询串或控制台分隔符，总长度不超过 **127 ASCII 字节**。ReHLDS 允许 128 字节，但当前客户端的缓冲区含终止符只有 128 字节，故采用更严格限制。

普通 CS 会请求原始资源路径。HTTP 服务即使收到 `Accept-Encoding: gzip` 也返回原始字节；仅可选预取器请求显式压缩 URL。当前没有向原生客户端提供透明 gzip/bzip2 的兼容性承诺。预取器同时验证压缩文件及解压结果的大小和 SHA-256，拒绝截断、尾随流和超额解压，压缩失败尝试原始文件。

保持 `sv_allow_dlfile 1`，否则配置 FastDL 后 ReHLDS 会主动拒绝普通文件的游戏通道下载。回退仍受客户端下载设置和服务端安全规则限制；特别是 `.sc` 事件扩展名只有两字符，当前 ReHLDS 的游戏通道拒绝它。清单逐文件用 `udpFallback` 标明该静态路径兼容性，不代表已通过实际入服。原生 HTTP 路径继续使用原有引擎一致性规则，不具备本工具可信清单的 SHA-256 保证。

## 并发、取消和文件边界

HTTP 服务默认 8 个工作线程，可配置 1–32，超过容量返回 503。支持 HTTP/1.0、GET、HEAD、单段 Range、ETag 和 If-Range。每个请求关闭连接，套接字默认超时 5 秒；关闭服务会终止活动连接并等待线程收尾。它是有界发布工具，不承担公网 CDN 的 TLS、限流和运维功能。

预取默认 4 个工作线程，可配置 1–8；套接字默认超时 3 秒，文件响应体默认每次尝试最多 60 秒。清单响应体最多 8 MiB/30 秒；单文件最多 16 MiB 减一字节，清单最多 16384 文件/4 GiB。取消会停止新任务并清理未完成的临时文件，已完整验证并提交的文件保留。网络超时作用于套接字操作；系统 DNS 和 HTTP 响应头解析不属于响应体总时限，不能把这些值宣称为任意网络条件下的严格退出上限。

下载先写同目录临时区，校验后以原子、不覆盖目标的方式提交。目录锁阻止两个预取器同时修改一个目标。异常退出留下的 `.fastdl.lock` 只有确认没有预取进程后才可由维护者移除。目录必须由操作员管理且不被其他本地进程恶意换链；路径检查不宣称抵抗拥有同目录写权限的本地攻击者在检查后替换目录。

资源名采用保守白名单，拒绝目录穿越、编码路径、绝对路径、Windows 设备名、链接/junction、配置/脚本/二进制插件及大小写冲突。游戏文件名要求 ASCII；游戏安装根目录可以包含中文、空格和单引号。Windows 扩展路径前缀会规范化后再执行目录约束，避免并发建目录时的误判。

## 下载职责与证据

| 层 | 实际职责 | 已核验依据 |
| --- | --- | --- |
| ReGameDLL / AMXX | 用 precache 声明地图、模型、声音和其他资源 | ReGameDLL `world.cpp`、ReHLDS `PF_precache_*` |
| ReHLDS | 生成资源列表，发送 `svc_resourcelocation`，处理 `dlfile` 游戏通道回退 | `SV_SendResources_internal`、`SV_CreateResourceList`、`SV_BeginFileDownload_f` |
| GoldSrc 客户端 | 判断缺文件，排队 HTTP 请求、保存到 GAMEDOWNLOAD，完成后重连，必要时转游戏通道 | 匹配 10210 `hw.dll` 静态检查 |
| GoldCraft 扩展列表 | 保留原生 HTTP 地址与最终下载入口 | `native/engine/precache.cpp`、`native/client/precache_client.cpp` |
| 本工具 | 完整清单校验、独立发布、受限 HTTP 服务、可选校验预取 | `tools/fastdl` |

固定源版本见 `sources.lock.json`：ReHLDS `550f2d62f13f4ebeb029c1d9d1c212133202611d`、ReGameDLL `4a50c42e85fd3778c2b7d24731c7d30c83bbab01`。工作树含 GoldCraft 修改，不能将本地行号等同于未打补丁的上游版本。

静态检查的客户端为 x86/build 10210，`hw.dll` SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`。定位到 HTTP 地址解析、缺文件分流、最多 5 个活动请求的调度条件、保存文件、结束后 `retry` 及取消释放路径。原生路径因此无需新增 MetaHook 插件或修改共享 CMake/入口。并发数字是该二进制的静态条件，尚未计量实际连接；其他客户端版本不能直接套用。

## 复现与验收状态

```powershell
python -B tools/fastdl/test_fastdl.py --work analysis/fastdl-regression-new
```

测试仅使用新建夹具和环回临时端口，低优先级运行；报告及失败证据留在指定目录。Windows 下包含真实 junction 拒绝、真实删除共享锁、中文安装根目录和扩展路径检查，另有 HTTP 并发/过载、Range、取消、gzip 回退、损坏/截断/压缩炸弹、冲突文件保留、锁及源文件变更拒绝。

2026-10-16 实际沙盒四资源合计 1586264 字节，压缩传输 704900 字节，减少 55.6%；校验与源文件不变通过。回环原始顺序读取约 37.97 MiB/s；包含落盘/fsync 的预取单线程约 0.432 秒、四线程约 0.406/0.403 秒。这些不同路径的测量不能作为公网加速倍数，也没有 UDP 基线。

本地证据在 `analysis/fastdl-20261016`：`tests*/result.json` 与日志保留每轮结果；`real-assets01/fixed-result.json` 为修复后的实际传输；`client-engine/*.json` 为匹配二进制静态证据，不进入源码发布。尚需在主调度安排的隔离窗口验证：普通 CS 缺文件 HTTP 成功、HTTP 404/不可达后可回退资源的 UDP 成功、取消/重连、实际地图/模型/声音加载，以及同一资源集的网络吞吐。当前运行服和正式 CS 安装未因这项工作修改。
