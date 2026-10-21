# CSOL 武器客户端接入与 HTTP 资源交付

本轮将武器呈现接入独立 MetaHook V4 插件 `CSOLSupport.dll`，并让 ReHLDS 自动预缓存关联资源。插件已部署到项目客户端 B，并由 B 实际连接隔离专服完成资源下载与武器画面检查。完整 CSOL 游戏表现尚未验收；正式专服和 HLAM 本轮未更新。

## 资源如何进入客户端

1. `tools/Prepare-CSOLResources.py` 调用 `csol_resources.exe` 扫描指定主模型，将模型、JSON、SPR、事件声音、外部贴图、默认手部、反射贴图和已验证部件复制进服务器游戏目录。保留原始编码；已有同名文件哈希不同则停止，不自动覆盖。
2. ReHLDS 的 `PrecacheModelSpecifiedFiles` 调用 `GoldCraftPrecacheCsolResources`，将依赖加入引擎资源清单。
3. 内建 HTTP 服务只发布实际预缓存的资源。客户端原下载流程从 `sv_downloadurl` 获取缺失文件，写进 `cstrike_downloads`。
4. `CSOLSupport.dll` 从引擎文件系统加载下载后的文件，在内存中解码 MDL 20/21 与 SPR v3；不修改源文件。

`CSOL_RESOURCE_ROOT` 和 `-csol_resources` 仅为显式本地研究入口。本轮下载验收清除了该环境变量且未传入该参数，客户端不能依赖解包 common。

HTTP 配置示例用于本机隔离测试；远程服务器须使用玩家可访问的地址：

```cfg
sv_allowdownload 1
sv_send_resources 1
sv_allow_dlfile 1
mc_http_download 1
mc_http_bind "127.0.0.1"
mc_http_port 27180
mc_http_public_url "http://127.0.0.1:27180/goldcraft/"
```

固定路由必须包含 `/goldcraft/`。`mc_http_stats` 查看请求、字节和读取失败；客户端启用 `cl_allowdownload 1`。已有资源无需重复下载。

部署使用 `tools/Deploy-CSOLSupport.ps1 -VerifiedClient <已验证沙盒客户端绝对路径>`；先加 `-ValidateOnly` 可只核对计划。脚本校验引擎、原插件、文件系统和 gamedata，共 229 项与已验证副本一致后，备份目标 DLL/插件表，再追加独立插件；不批量替换其他插件。首次比对发现隔离客户端文件系统与 B 不同，已将隔离副本换成 B 的原版本并重新通过冷下载/画面验证，才执行 B 部署。

## 武器状态

`CsolPart` v2 为 18 字节、小端序：version u8、slot u8、modelIndex u32、sequence/skin/body/ammo/maxAmmo/flags 各 u16。保留 16 字节 v1 兼容，slot 为 0 或 1，flags bit0 为射击后展开状态。

`CsolView` v1 为 11 字节：version u8、modelIndex u32、skin u16、body u32。状态由服务端武器逻辑发送；插件只在当前模型绘制期间覆盖 skin/body，完成后恢复预测实体。消息回调不加载 GPU 资源。每槽最多保留一个待处理消息，模型匹配后消费，2 秒到期或时间回退时清除。

SoulReaper EX 的界面皮肤 2 对应引擎 skin 1，C 模式 stone bodygroup 为子模型 1；界面皮肤 3 对应引擎 skin 2。插件不能把普通状态强制锁为充能。测试 Pawn 只负责呈现夹具，不能当作完整武器伤害/弹药实现。

左右手使用原生 `cl_righthand 0/1`，复活枪会选择相应附着 SPR。`csol_enable` 控制接管；`csol_hand` 可指定已有手部外观资源，空值使用当前角色名称及默认回退。

## 证据与结论

证据位于本地 `build/csol-runtime-20261021`，不随源代码发布；运行命令依赖用户已有且有权使用的本地游戏素材。

| 证据 | 观察 | 结论与调用路径 |
| --- | --- | --- |
| `http-final-cold-verification.json` | 原 68 文件全部在客户端缺失，HTTP 新增 68 次请求，逐文件 SHA-256 一致，客户端无下载错误，实际入服 | ReHLDS 依赖清单 → HTTP → `cstrike_downloads` 路径已实测；首尾文件落盘约 3.98 秒，仅为本机数据 |
| `http-fresh-cold-preflight.json`、`http-fresh-cold-verification.json` | 新进程启动前，70 项资源在客户端及备用目录全部缺失；HTTP requests=70、loads=70、rejected=0、failures=0，70 项 SHA-256 全一致，实际 1 玩家入服 | 含两张带 `#512512` 文件名的手部贴图全部通过 HTTP；首尾落盘约 4.19 秒，仅为本机数据，无 UDP Processing 或下载错误 |
| `http-b-runtime-verification.json`、`http-formal-b-verification.json`、`formal-b-loaded-modules.json` | 对齐文件系统后的副本及 B 本体分别冷下载70项，SHA全一致，每轮新增70次HTTP读取、拒绝/失败0；B真实加载独立插件，首尾落盘约3.88秒 | B部署后资源下载/加载成立；未向B预拷贝武器素材或挂载common |
| `formal-b-render-verification.json`、`formal-b-lifecycle.json` | B实际武器样本失败0/glError0，重新单独发送Soul Draw确认sequence3；重连无新增下载；换图状态清空，玩家重新入服后发送parts2成功 | 区分切换武器重置动作及换图重连尚未完成的夹具时序，不能将早发命令的空状态当作渲染失败 |
| `http-render-verification.json`、对应 BMP | 复活枪炫彩/换弹、SoulReaper EX Draw/skin1 BC、Gungnir charge、LeapStrike EX 部件及普通 AK 实际绘制 | 下载后的模型/JSON/SPR 可被独立插件使用；截图诊断 glError=0 不代表完整 GL 状态验收 |
| `http-clock-parts.json`、`http-parts-comparison.png` | 五条部件消息分别进入 0/2/0/1/3，拒绝计数 0，同相机下几何展开/收回 | 消息接收用 GetClientTime，消费也须用同一时钟；StudioGetTimes 略滞后会误判为时间回退 |
| `http-hands.json`、对应 BMP | 左右手复活枪和中央附着均显示 | 原生左右手切换可用；不同人物专属手部资源的完整服务器交付尚未覆盖 |
| `http-reconnect-verification.json` | 实际重连成功且 HTTP 请求/字节不变，无新下载错误 | 已有下载资源复用成功 |
| `http-tests.log` | 实际生产 server/store/adapter 测试通过，含新增类型、百分号编码路径、未预缓存 404 | 文件类型放行仍受实际预缓存白名单及路径校验约束 |
| `codec-tests.log` | 13025 checks，117 模型、4 SPR、154 RGBA 帧通过 | CPU 格式/状态/既有原引擎执行向量检查；不能代替画面验收 |

后续核对原始文件及 HLAM `CSOLModelSource.cpp` 发现 `#512512` 是外部贴图文件名的一部分，不能剥掉。已修复共享路径解析并将两张手部 BMP 加入依赖，清单增至 70 文件、94,745,652 字节。GoldSrc 会在 HTTP 请求目标中原样发送 `#`，服务端须按完整预缓存文件名匹配，同时接受 `%23`；不能将井号之后的内容当作 fragment 截断。追加后缀或未预缓存文件仍返回 404。

首次 70 文件尝试有两张贴图退回 UDP；修正服务端后，沿用旧客户端进程重连仍未发起 HTTP，因此不能把原进程的失败状态当成新实现验收。关闭后启动全新客户端，重新核验全部资源缺失，再取得上述 70 项全 HTTP 成功记录。随后重新执行 `verify-http-render.py`，11 个画面样本均模型失败 0、诊断 glError=0；新日志无缺外部贴图警告，部件 0→2→0 正常应用。日志和截图不代替完整 GL 状态验收。

## 仍需完成

- 正式专服接入和通用服务器武器接线；目前隔离专服夹具用六模型及呈现消息验证。
- 世界/第三人称武器效果、阴影/GBuffer/OIT、全部材质和动态灯一致性。
- 角色专属手部依赖、部件声音、别名/配置重定向/贴图前缀、E 备用特效的玩法状态选择。
- 独立 GL 状态完整验证、内部 Studio 临时状态审计，以及固定视角实际性能测量。

源代码编译、CPU 测试、真实 HTTP 交付和实机画面分别留证，不以其中一项替代完整 CSOL 还原。
