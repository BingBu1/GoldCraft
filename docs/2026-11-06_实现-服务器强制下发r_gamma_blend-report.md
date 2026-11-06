# 实现：服务器强制下发客户端 r_gamma_blend（交主协调合批）

用户 2026-11-06 要求：“由服务器强制下发配置并通告给GPT”。背景见 `docs/2026-11-05_修复-龙炎拖尾每发被清空-report.md` 第“2026-11-06 构建部署与实机结果”节：B（与用户普通安装相同）`r_gamma_blend 0` 使 Renderer 在线性空间叠加透明特效，低透明度加法粒子（龙炎拖尾、BOSS 尾迹等）被压暗约两个数量级；原 CSO hw `378EDD90`/`378EE07D` 在伽马空间叠加。用户在 B 上手动设 `r_gamma_blend 1` 后确认龙炎与原版一致。

**性质**：宿主适配（MetaHookSv Renderer 配置），不是 CSO 原版数据或逻辑；原版没有对应的服务器下发。

## 行为

- 服务器 cvar `csol_gamma_blend`（`FCVAR_SERVER`，默认 `1`）：`-1` 不干预客户端，`0`/`1` 强制客户端 `r_gamma_blend`。其他值记 `at_error` 并按不强制处理。
- 每张图写入 sign-on 缓冲（`MSG_INIT`），每个新连接（含换图重连）都会收到；**图内改 cvar 要到下一张图才生效**。
- 新用户消息 `CsolRender`，1 字节：`0`/`1` 强制值，`255` 不强制，其余字节拒收。
- 客户端（CSOLSupport）：收到后只改 `r_gamma_blend` 的 `value`，每帧重新应用（玩家在控制台改回也会被覆盖）；**不改 `string`**。原因：引擎 `Host_WriteConfiguration` 在 `ClientDLL_Shutdown` 之前执行（ReHLDS `host.cpp` `Host_Shutdown`），如果整个 cvar 都被改，连着服务器直接退出时，强制值会写进玩家的 `config.cfg`。只改 `value` 时，存盘写的仍是玩家自己的选择。会话结束（换图/断开/资源会话重置）时按 `string` 恢复 `value`。没有 Renderer（cvar 不存在）时不写任何东西。
- 代价：强制期间控制台查询 `r_gamma_blend` 显示的是玩家保存的值，实际渲染用的是强制值；诊断 `renderSettings.gammaBlendValue` 显示实际值。

## 改动（源码）

| 文件 | 内容 |
| --- | --- |
| `native/csol/render_settings.hpp`（新） | 消息名、编解码 |
| `native/csol/client/render_settings.hpp/.cpp`（新） | 接收、每帧应用、会话恢复、诊断 |
| `native/csol/client/plugin.cpp` | `InitializeRenderSettings()`、`RenderSettingsFrame()` 各一行 |
| `native/csol/client/session.cpp` | `ResetSession` 末尾 `ResetRenderSettings()` |
| `native/csol/client/scene.cpp` | 诊断链加 `RenderSettingsDiagnostics(file)` |
| `native/csol/CMakeLists.txt` | 加 `client/render_settings.cpp` 一行 |
| `native/csol/server/weapon_registry.cpp` | cvar、`InitMessages` 注册 `CsolRender`、`CSOL_WriteWeaponDefinitions` 开头写 sign-on（在 `haveExtendedDefinitions` 提前返回之前） |
| `native/csol/server/weapon_slots.h` | 声明 `CSOL_InitRenderSettings()` |
| `native/csol/server/wpn_tknifeex2.cpp` | `CSOL_InitWeapons` 中调用 `CSOL_InitRenderSettings()` |
| `tests/csol-render-settings/client.cpp`（新） | 完整生产客户端 TU 测试 |

plugin/session/scene/CMake 的工作树里另有其他会话未提交的改动（如 `ResourceReadsAllowed`），本次只加上表所列行。

## 验证

| 层级 | 结果 |
| --- | --- |
| 客户端 TU 测试 | `build/csol-balrog-trail-20261105/render-01`：273 项 0 失败（编解码边界、强制/玩家改回再强制/会话恢复/服务器取消强制/坏字节/错长度转发/无 Renderer） |
| 生产编译参数 TU | 服务器 `weapon_registry.cpp`（`build/monster-skills-20261104/rs-srv-registry`）、`wpn_tknifeex2.cpp`；客户端 `render_settings.cpp`（`build/csol-balrog-trail-20261105/render-tu-01`）、`plugin.cpp`、`session.cpp`、`scene.cpp` 全部编译通过 |
| 完整 DLL、部署、实机 | **未做** |

## 交主协调

1. 合批服务端 mp.dll 与客户端 CSOLSupport.dll，两端都需要；只更新一端时：旧服务器不发消息（客户端不变）；旧客户端收到未挂钩的 `CsolRender` 由引擎忽略。
2. 部署后在 B 验证：保持 B 的 `config.cfg` 中 `r_gamma_blend "0"`，进服后诊断 `renderSettings.forcedGammaBlend=1`、`gammaBlendValue=1`，龙炎火焰与原版一致；断开后 `gammaBlendValue` 恢复为 0，退出后 `config.cfg` 仍为 `"0"`。
3. 当前 B 的 CSOLSupport 是 `5be5fa8a…`（龙炎/BOSS 尾迹修复），不含本功能。

## 附：特效无法遮挡普通僵尸（同日用户报告）

现象：黑龙炮火焰、龙炎火焰和迷雾僵尸放的雾都盖不住普通僵尸，巨型僵尸正常。B 实机截图 `build/csol-balrog-trail-20261105/capture-06/04-after.png`：窗外远处的普通僵尸清晰地压在近处的黑龙炮火焰上。

根因（客户端，非原版逻辑）：`native/csol/client/scene.cpp` `DrawWorldWeaponPoints` 会接管 `asset->special` 的工作室模型（代码注释：CSOL map effects and monsters carry the same external material policy），加入 `worldDraws`，在 Renderer 的 post-scene 回调 `DrawOwnedWorld` 里绘制。post-scene 在全部半透明实体、TriAPI 和粒子之后执行（Renderer `R_RenderScene`：不透明 → `R_EndRenderOpaque` → `ClientDLL_DrawNormalTriangles` → `R_DrawTransEntities`（含 `ClientDLL_DrawTransparentTriangles`）→ post-scene），所以这些僵尸会画在火焰和雾上面，但仍被墙的深度挡住。巨型僵尸资产不是 special，由 Renderer 在不透明阶段绘制，所以正常。怪物表 column21 盘点确认普通僵尸生成时是 `kRenderNormal`（`build/csol-balrog-trail-20261105/render-inventory/rows.jsonl`，4522/4786 行为 0,0,0）。

改动：
- `scene.cpp` 新增 `DrawOwnedOpaque()`：在 `HUD_DrawNormalTriangles`（GBuffer 已合成、半透明尚未绘制）对与当前主视图 FBO（`GetBackBufferFBO`/`GetBackBufferFBO3`）、帧号和矩阵完全一致的 `worldDraws` 先画不透明网格（pass 0，正常写深度），并标记 `opaqueDrawn`；沿用捕获时的光照，颜色空间沿用同一目标上一帧 post-scene 的 `META_POST_SCENE_GAMMA`。`DrawOwnedWorld` 对已预绘制的条目只画半透明网格与效果。未匹配的条目（阴影/水面/门户视图或首帧）保持原有 post-scene 全量绘制。
- `plugin.cpp` 挂 `HUD_DrawNormalTriangles`（先调下游）；`runtime.hpp` 声明；诊断加 `opaquePrepass` 计数。

验证：`scene.cpp`、`plugin.cpp` 生产参数 TU 编译通过（`build/monster-skills-20261104/op-cli-scene`、`op-cli-plugin`）。为实测，以 B 已部署 DLL 的冻结源码为底只叠加本会话改动（龙炎/BOSS 尾迹、render settings、opaque prepass），构建 `build/csol-balrog-trail-20261105/client-dll-04`（`build_client_dll2.py`），CSOLSupport `dd763ddb…`，已部署 B（备份 `sandbox/cs-client-b/backups/csolsupport-20261106-105729` 为上一版 `5be5fa8a…`；`csolsupport-20261106-094645` 为最初的 `4a061818…`）。用户在 B 实机确认已修复（特效可正常遮挡普通僵尸）。服务器未改，`CsolRender` 尚无服务器发送。

交主协调：B 现运行的 `dd763ddb…` 只是“冻结底 + 本会话改动”的验证构建，正式客户端需按工作树真实依赖重新合批；需要合入的源码为 `balrog11_flame.cpp`、`elite_visuals.cpp`、`render_settings.*`、`scene.cpp`/`plugin.cpp`/`session.cpp`/`runtime.hpp` 中本报告所列行、`CMakeLists.txt` 一行，服务器端见上文。阴影/水面/门户视图中的这类模型仍走原 post-scene 路径，未实测。

## 建议的共享文档更新

`TODO.md`：
```
- [ ] 合批部署服务器强制 r_gamma_blend：csol_gamma_blend（默认1，-1不干预）经 sign-on 消息 CsolRender 下发，客户端只改 value 不改存盘 string，会话结束恢复；客户端测试 273/0、改动 TU 生产编译通过，未构建/部署/实测。详见 docs/2026-11-06_实现-服务器强制下发r_gamma_blend-report.md。
```
