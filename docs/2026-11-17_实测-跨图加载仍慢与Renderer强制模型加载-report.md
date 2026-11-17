# 跨图加载仍慢与 Renderer 强制模型加载

四文件模型读取优化已更新到单个沙盒 B，穷途末路和失落之城之间两次跨图均最终入服；**换图故障仍未修好**。真实画面仍出现旧地图上的 Connection Problem，随后才进入空的原生连接进度条，观察器记录到未响应。不能把读取组件测试通过或最终入服等同于换图体验通过。

## 部署和测试范围

用户明确批准本地四文件部署和单 B 换图实测。部署包括 CSOLSupport.dll、匹配 PDB 和 goldcraft-precache 的目录 JSON/index；DLL SHA-256 为 `60ea66b58aa43daf6e96d79f516b474018bf11b556eb80d88c234f7108b138b8`。保留已安装 Renderer_AVX2 `04e8bf1f`、GoldCraft `a7de393f`、ParallelDownload `96adc0a4` 和既有专服 MP `f9a25326`。

完整服务器 HUD 319 项与 Renderer/资源刷新接口预检通过。四文件按已封存成品逐 SHA 替换并保留完整回滚，未编译当前盲盒或其他工作区改动。Steam 原安装五项基线未变，未更新正式服务器。部署回执在 `build/map-change-live-20261117-02/deployment.json`。

初次部署脚本把 GameUI.dll 的只读基线路径写成根目录，读取失败后即停止，未写运行文件。核实其实际位置为 valve/cl_dlls/GameUI.dll 后，新的部署记录通过；未绕过保护。专服初次管理查询超时后复查同一 PID，确认仍活跃并最终正常响应，没有另启重复专服。

启动封装等待子进程超时，但实际 B 已运行并持续更新状态；通过进程路径、启动时间和实时状态确认后继续使用同一进程，没有因封装超时再次启动。

## 证据和结果

| Evidence | 实际观察及本地来源 | 验证边界 |
| --- | --- | --- |
| E-001 | `deployment.json`、`client-modules.json`；单 B PID 4492、专服 PID 12332 | 本地成品部署和模块身份；不是正式服验收 |
| E-002 | `initial-status.json`、`lostcity-status-after.json`、`deadend-return-status.json` | 初次入图原生恢复 13.301 秒；切失落之城 13.700 秒；返回穷途末路 17.166 秒。这些是 inclusive 组件计时，不能相加作整段延迟或宣称与旧版同条件提速 |
| E-003 | `cross-lostcity.json.raw`，18,902 个真实主线程上下文，含 CPU/进程 I/O；`cross-lostcity.json` 为独立符号解析结果 | 非调试器启动。墙钟栈包含阻塞，不能把样本占比当独占 CPU。解析很慢时保留同一 profiler 句柄，不重新采样或宣称失败 |
| E-004 | `back-deadend-observations.json`，603 次状态/IsHungAppWindow 观察，覆盖 65.09 秒 | 22 次 hung 观察约位于 36.70～38.91 秒；新清单首次在 39.86 秒被观察到，本窗口内尚未完成全部入图。最终返回由 E-002 和专服 status 另证；窗口切前台混入，不能作严格性能 A/B |
| E-005 | 修复截图组件后，本对话通过原生 sky 实际看见失落之城、人物头像、武器卡、CSOL 金钱与体力条；随后看见 Connection Problem 和“正在建立与服务器的网络连接…”空条 | 原截图工具两次超时已转交用户指定“测试截图功能”，对方修复后本对话复验。后续物理 Escape 再次停止界面；画面流程没有整轮通过 |
| E-006 | 运行栈 `Renderer_AVX2!GL_BuildLightmaps+0x120 [rva=0x103a40]` → 原生模型查询 → `LoadStudio`；匹配 PDB 定位 gl_rsurf.cpp:421 | 已解析的全部这类 Studio 加载栈均经过该循环。该循环在过滤 brush 前调用 hudGetModelByIndex，原接口可同步加载延迟模型 |
| E-007 | 现有宿主 IDA 证据：hw.dll SHA `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，CL_GetModelByIndex VA `0x101A2FA0` / RVA `0x1A2FA0`；原 precache 懒加载分支 RVA `0x1A462B` | 原按需加载入口与 current precache_client.cpp 既有证据一致。Renderer 适配需保留该普通查询语义，不全局改成不加载 |

完整原始证据与私有运行信息留在工作区，不上传源码仓库。可以在 Git Bash 使用已有 `tools/Profile-NativeClient.cpp` 构建的 x86 采样器对准确 PID、可执行路径和最长 60 秒执行采样；只读窗口状态观察器和精确启动命令保存在 `analysis/model-read-span-20261117`。原证身份、撤回候选与基础修复见[读取事务报告](2026-11-17_优化-原生模型读取事务复用-report.md)。

## 根因链和下一步

F-001，status=validated、severity=n/a_re、confidence=high，依据 E-003/E-006/E-007：原先只登记的角色模型被 Renderer 建图扫描重新主动加载。`deferredModels=170/172` 证明执行过登记分支，不能证明模型持续保持延迟状态。仅有该计数不足以接受完整加载优化。

P-001，path_type=callflow：已核 SHA 清单 → precache_model 只登记 models/player 名称（E-007）→ Renderer GL_BuildLightmaps 遍历全部服务器模型（E-006）→ hudGetModelByIndex 先触发原生加载，再检查 mod_brush（E-006/E-007）→ Studio 解码、动画验证和纹理准备（E-003）→ 迟来的入图恢复（E-002/E-005）。

下一步应为建图用途提供无副作用的模型枚举，或准确区分已加载的 brush 与待加载 studio；不得复用“已知模型表索引”充当服务器媒体索引。还需检查 R_LoadWorldResources 和 R_Reload_f 的同类扫描。普通实体实际取模型仍应按需加载，BSP/子模型、普通服务器及资源退役生命周期必须保留。新接口和新行为尚未实现、构建或部署。

实际顶部指令还定位到 Animation 的逐字节角色标记和 memmove。已有 memchr/memset 离线候选曾慢 12.7～15.3%，已拒绝，不能未经重测重新采用。先处理 Renderer 强制加载的生命周期缺口，保留完整 SHA/VFS/CRC/RLE 检查和效果；后续优化须重新测量。

本轮未启用或交付武器盲盒；同一 JSON 奖池和目录来源字段只是未构建源码，检查点位于 `analysis/custom-blind-box-20261117/CHECKPOINT.md`。灾变 AI 没有改变。截图恢复、实际跨图和局部性能证据均不足以关闭完整大灾变、自定义玩法、公网或多人目标。
