# SKULL-3 部署后的专服资源广告与 HTTP 验证

新版专服已完成 353 项客户端资源的真实广告及 HTTP 内容验证，全部通过。首轮发现的五项 HUD 资源缺失已经消除，原失败记录完整保留。验证只操作本地专服，不启动客户端，不使用界面输入，不写正式远端。

## 已执行范围

已成功核对父任务部署回执 `build/skull3-audit-20261105/deployment-prep/transaction-01/receipt.json`，回执 SHA-256 为 `5f7a5086ca3b17fcb78ef2a98efc84baaeab53f5487c97d02b2d65089ab390d6`，然后启动唯一的本地专服 PID `44808`。

实际运行状态是 `zs_deadend`（穷途末路）、32 槽、`csol_game_mode=2`、`sypb_quota=0`，UDP 27045 与 HTTP TCP 28080 均只监听 `127.0.0.1`，监听者均是该专服。当前没有客户端连接。

已读进程模块路径并核对实际文件：

| 模块 | SHA-256 |
|---|---|
| mp.dll | `5d1e1c94c3f442611709a8aee0a874d58b8c52081826689eefb3dda7bc0862ec` |
| swds.dll | `8b26eba6c00de72e3b2456c52ec412a33daff711444babf97428255744a19dc0` |
| filesystem_stdio.dll | `14bedc219a27811576faacddc316ae522e566c802f9c3852be1896277a7ac553` |

实际读取四类 `reslist`：model 528、sound 418、event 42、generic 2101 项。部署清单中的 354 项逐项核对服务端文件内容；其中 353 项必须供客户端获取，`csol/weapons/skull3_damage.json` 仅供服务端使用，不要求广告或 HTTP。

## 首轮失败与定位

`live-server-01/result.json` 保留原始失败，不覆盖成通过结果。五项未广告且 HTTP 404：

- `models/csol/hud_v7.json`
- `resource/csol/hud/v1/skull3_ammo.tga`
- `resource/csol/hud/v1/skull3_weapon.tga`
- `resource/csol/hud/v1/skull3d_ammo.tga`
- `resource/csol/hud/v1/skull3d_weapon.tga`

实服广告中仍有 `models/csol/hud_v6.json`。父任务已定位到服务端冻结输入未同步新版共享 HUD 头文件，正在准备窄增量构建；这五项的根因是缺少广告，不是 HTTP 引擎无法服务已经广告的文件。

另一个 404 来自验证器请求的拼写：部署资源路径使用 `.BMP`，引擎实际广告使用 `.bmp`。按实际广告 `models/texture/#512512CSO_HighQ_Hand__Long_512.bmp` 请求，返回 200、263222 字节、SHA-256 `51f40b90e3fa8f4ff04fc347363a23d35317ad0f5d4e56cbab8559e782e6c073`，与部署文件完全相同。这一项属于验证器误报，没有修改 HTTP 引擎。只读复核保存为 `live-server-01/failed-resource-recheck.json`，原始结果文件逐字节保持。

首轮没有新增 AMXX 错误；配置文件、B 客户端资源及受保护安装校验均保持不变。专服保留运行，没有通过重启掩盖首败。

## 后续核验与验收边界

已准备 `live-server-01/verify_running.py`，只读取指定 PID 的现有专服，不自行重启。它按大小写折叠关联部署资源与真实广告，实际 GET 使用广告原始拼写；每个实际响应仍必须匹配部署文件的完整长度和 SHA-256。新版服务端部署并启动后，将输出到独立的 `live-server-02` 目录。

父任务随后只更新了本地专服 `mp.dll/mp.pdb`，新版 DLL SHA-256 为 `58ab5bfc044245ebc1a981f50e75f183a288833db2fe22dde80ef3cccf783e69`。本子任务的后续启动器绑定 `hud-sync-deploy-01/receipt.json`（SHA-256 `8c763c2eae4dfab3f1dea23e6876ba24d2aef770c7e705b7267e47adda8e2cd1`），没有沿用首轮旧 MP 身份。

唯一新专服 PID `31152` 启动后，第二轮预检查发现 `game_init.cfg` 将下载广告地址设回 27080，而实际 HTTP 监听端口为 28080。`live-server-02/result.json` 与 `live-server-02-launch/launch-result.json` 保留该失败。按父任务确认的已有本地测试设置，仅将当前进程的 `mc_http_public_url` 与 `sv_downloadurl` 恢复到 `http://127.0.0.1:28080/goldcraft/`，原值和新值写入 `live-server-02-launch/runtime-url.json`；没有修改配置文件，也没有再重启。

第三轮 `live-server-03/result.json` 通过，实际数据如下：

- 353 项客户端资源均存在真实广告，353 个实际广告 URL 均返回 HTTP 200；长度、Content-Length 和完整 SHA-256 均与部署文件一致，共 40,625,905 字节。
- 新版 HUD 清单与 SKULL-3/SKULL-3D 四项图标均已广告且成功获取；手模按实际 `.bmp` 广告拼写成功获取。
- 服务端独占的伤害 JSON 在磁盘内容检查通过，未要求广告给客户端。
- 穷途末路、32 槽、灾变模式 2、SyPB 0 均正确；TCP 28080 和 UDP 27045 仅监听 `127.0.0.1`，均由 PID 31152 持有。
- 没有新增 AMXX 错误；配置文件、B 客户端资源和受保护安装的已登记基线均保持不变。

完成后将专服保留运行并交还父任务。资源广告/HTTP 通过只证明下载链路，不代表客户端成功连接、HUD 画面、SKULL-3 配对购买丢拾或噬魂命中玩法已经验收。本子任务没有启动 B 或使用界面输入；父任务另行处理用户恢复的单 B 实测授权。

## 后续客户端来源复核：HUD6

父任务启动 B 后，352 项与初始部署清单匹配，唯一 HUD6 不同。独立复核发现客户端保留的 `de24e71d…` 才是正确 `cstrike_chn` 原图，服务端新装的 `e9b6481c…` 是 merged common 中的 Valve 备用图；因此此前 HTTP 检查只证明与暂存清单一致，不能作为原件正确的证据。当前两份 NAR 对应条目也已只读重核，解出 SHA 与历史原件一致。

原 `hud.txt` 与当前死亡图消费者分别使用 SKULL-3 `(172,128,48,16)`、SKULL-3D `(172,144,48,16)`、cannon `(172,160,48,16)`。后两区域在 Valve 图是单色占位；SKULL-3 区域则是不同的非单色图，不能用“是否单色”泛化来源判定。选枪图与弹药图来自 68/2，不受此图影响。

父任务曾在线独立原子恢复正确 HUD6，并通过 `mc_http_refresh` 更新 HTTP 正文；回执 `hud6-correction-01/receipt.json` 证明响应由 e9 变为 de24、200/66366B，B 正确缓存保持。当时关于“换图后可安全刷新”的判断已撤回：原进程保留首次模型 CRC，随后通关换图触发了真实 `Mod_LoadModel` 校验终止。原回执不改写；完整 dump 证据和冷重启处置见[CRC 终止调查](2026-11-05_排查-通关重载HUD资源CRC终止-report.md)。此类资源必须停服更新再启动，HTTP 正文通过不代表进程缓存可安全切换。B 的 353 项内容核同只证明磁盘内容。

为防准备阶段再次覆盖正确来源，`Prepare-CSOLResources.py` 新增独立 HUD atlas 的挂载顺序读取及可选 `--source-overrides`。override 必须绑定相对原路径、当前 base 长度/SHA 和候选长度/SHA；其他模型/材质图不变。10 项来源测试、真实 35 文件 collector 闭包及 3 个落盘前拒绝案例通过，独立候选仅 HUD6 一项变化，其余 34 项保持。没有修改 C++、DLL、common 或客户端资源。使用与边界见 `tests/csol-resource-sources/README.md`；详细证据在 `live-client-01/stale-hud-review/`。

本地 B 已持正确原图，无需为它版本化错误资源；若其他客户端曾下载错误 e9，仍需另行处理同名缓存更新。此次未声称覆盖那类外部分发状态。
